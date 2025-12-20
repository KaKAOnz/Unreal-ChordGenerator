// Copyright ChordPBRGenerator

#include "GeminiApiClient.h"
#include "ChordImageUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Async/Async.h"

void FGeminiApiClient::GenerateImageAsync(
	const FString& ApiEndpoint,
	const FString& ApiKey,
	const FString& Model,
	const FString& Prompt,
	FOnGeminiImageGenerated OnComplete)
{
	if (bIsRequestInProgress)
	{
		OnComplete.ExecuteIfBound(nullptr, TEXT("A request is already in progress."));
		return;
	}

	if (ApiKey.IsEmpty())
	{
		OnComplete.ExecuteIfBound(nullptr, TEXT("Gemini API key is not configured."));
		return;
	}

	bIsRequestInProgress = true;

	// Build the request URL
	const FString Url = FString::Printf(TEXT("%s/%s:generateContent"), *ApiEndpoint, *Model);

	// Build the request body
	TSharedRef<FJsonObject> RequestBody = MakeShared<FJsonObject>();
	
	// Contents array
	TArray<TSharedPtr<FJsonValue>> ContentsArray;
	TSharedPtr<FJsonObject> ContentObj = MakeShared<FJsonObject>();
	
	TArray<TSharedPtr<FJsonValue>> PartsArray;
	TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
	TextPart->SetStringField(TEXT("text"), Prompt);
	PartsArray.Add(MakeShared<FJsonValueObject>(TextPart));
	
	ContentObj->SetArrayField(TEXT("parts"), PartsArray);
	ContentsArray.Add(MakeShared<FJsonValueObject>(ContentObj));
	RequestBody->SetArrayField(TEXT("contents"), ContentsArray);

	// Generation config - request image output
	TSharedPtr<FJsonObject> GenerationConfig = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ResponseModalities;
	ResponseModalities.Add(MakeShared<FJsonValueString>(TEXT("TEXT")));
	ResponseModalities.Add(MakeShared<FJsonValueString>(TEXT("IMAGE")));
	GenerationConfig->SetArrayField(TEXT("responseModalities"), ResponseModalities);
	
	RequestBody->SetObjectField(TEXT("generationConfig"), GenerationConfig);

	// Serialize to JSON string
	FString RequestBodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBodyString);
	FJsonSerializer::Serialize(RequestBody, Writer);

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("x-goog-api-key"), ApiKey);
	HttpRequest->SetContentAsString(RequestBodyString);

	PendingRequest = HttpRequest;

	// Set up response handler
	HttpRequest->OnProcessRequestComplete().BindLambda([this, OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		bIsRequestInProgress = false;
		PendingRequest.Reset();

		if (!bWasSuccessful || !Response.IsValid())
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("HTTP request failed."));
			});
			return;
		}

		const int32 ResponseCode = Response->GetResponseCode();
		const FString ResponseContent = Response->GetContentAsString();

		if (ResponseCode != 200)
		{
			FString ErrorMessage = FString::Printf(TEXT("API error (HTTP %d): %s"), ResponseCode, *ResponseContent);
			AsyncTask(ENamedThreads::GameThread, [OnComplete, ErrorMessage]()
			{
				OnComplete.ExecuteIfBound(nullptr, ErrorMessage);
			});
			return;
		}

		// Parse JSON response
		TSharedPtr<FJsonObject> JsonResponse;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
		if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("Failed to parse API response."));
			});
			return;
		}

		// Extract image data from response
		// Response structure: { "candidates": [{ "content": { "parts": [{ "inlineData": { "mimeType": "...", "data": "base64..." } }] } }] }
		const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
		if (!JsonResponse->TryGetArrayField(TEXT("candidates"), Candidates) || Candidates->Num() == 0)
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("No candidates in response."));
			});
			return;
		}

		const TSharedPtr<FJsonObject>* CandidateObj = nullptr;
		if (!(*Candidates)[0]->TryGetObject(CandidateObj))
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("Invalid candidate format."));
			});
			return;
		}

		const TSharedPtr<FJsonObject>* ContentObjPtr = nullptr;
		if (!(*CandidateObj)->TryGetObjectField(TEXT("content"), ContentObjPtr))
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("No content in candidate."));
			});
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!(*ContentObjPtr)->TryGetArrayField(TEXT("parts"), Parts))
		{
			// Log the actual response for debugging
			FString DebugResponse = ResponseContent.Left(500); // First 500 chars
			AsyncTask(ENamedThreads::GameThread, [OnComplete, DebugResponse]()
			{
				FString ErrorMsg = FString::Printf(TEXT("No parts in content. API Response: %s..."), *DebugResponse);
				OnComplete.ExecuteIfBound(nullptr, ErrorMsg);
			});
			return;
		}

		// Check if parts array is empty
		if (Parts->Num() == 0)
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("Parts array is empty. The model may not support image generation or failed to generate an image."));
			});
			return;
		}

		// Find image part
		TArray<uint8> ImageData;
		for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
		{
			const TSharedPtr<FJsonObject>* PartObj = nullptr;
			if (!PartValue->TryGetObject(PartObj))
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* InlineDataObj = nullptr;
			if ((*PartObj)->TryGetObjectField(TEXT("inlineData"), InlineDataObj))
			{
				FString Base64Data;
				if ((*InlineDataObj)->TryGetStringField(TEXT("data"), Base64Data))
				{
					FBase64::Decode(Base64Data, ImageData);
					break;
				}
			}
		}

		if (ImageData.Num() == 0)
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]()
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("No image data found in response."));
			});
			return;
		}

		// Create texture from image data on game thread
		AsyncTask(ENamedThreads::GameThread, [OnComplete, ImageData = MoveTemp(ImageData)]()
		{
			UTexture2D* Texture = FChordImageUtils::CreateTextureFromImage(ImageData, TEXT("GeminiGeneratedImage"));
			if (Texture)
			{
				OnComplete.ExecuteIfBound(Texture, FString());
			}
			else
			{
				OnComplete.ExecuteIfBound(nullptr, TEXT("Failed to create texture from image data."));
			}
		});
	});

	HttpRequest->ProcessRequest();
}

void FGeminiApiClient::CancelRequest()
{
	if (PendingRequest.IsValid())
	{
		PendingRequest->CancelRequest();
		PendingRequest.Reset();
		bIsRequestInProgress = false;
	}
}
