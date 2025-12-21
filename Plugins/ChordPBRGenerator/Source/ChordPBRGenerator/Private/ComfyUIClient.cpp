// Copyright ChordPBRGenerator

#include "ComfyUIClient.h"

#include "HttpModule.h"
#include "Http.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "IWebSocket.h"
#include "JsonObjectConverter.h"
#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"
#include "Templates/Atomic.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"

namespace
{
	struct FWebSocketWaitState
	{
		explicit FWebSocketWaitState(const TSharedRef<FEvent, ESPMode::ThreadSafe>& InEvent)
			: CompletionEvent(InEvent)
		{
		}

		TAtomic<bool> bDone{ false };
		TAtomic<bool> bFailed{ false };
		FCriticalSection Mutex;
		FString Error;
		TSharedRef<FEvent, ESPMode::ThreadSafe> CompletionEvent;
	};

	FString NormalizeBaseUrl(const FString& Url)
	{
		FString Clean = Url;
		Clean.RemoveFromEnd(TEXT("/"));
		return Clean;
	}

	bool ParseJsonResponse(const FHttpResponsePtr& Response, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		if (!Response.IsValid())
		{
			OutError = TEXT("No HTTP response.");
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = TEXT("Failed to parse JSON.");
			return false;
		}

		return true;
	}

	void AppendJsonStringValues(const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutStrings)
	{
		if (!Value.IsValid())
		{
			return;
		}

		switch (Value->Type)
		{
		case EJson::String:
			OutStrings.Add(Value->AsString());
			break;
		case EJson::Array:
		{
			for (const TSharedPtr<FJsonValue>& Inner : Value->AsArray())
			{
				AppendJsonStringValues(Inner, OutStrings);
			}
			break;
		}
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			FString Message;
			if (Obj.IsValid() && Obj->TryGetStringField(TEXT("message"), Message))
			{
				OutStrings.Add(Message);
			}
			break;
		}
		default:
			break;
		}
	}

	bool TryExtractHistoryError(const TSharedPtr<FJsonObject>& History, const FString& PromptId, FString& OutError)
	{
		if (!History.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* PromptObj = nullptr;
		if (!History->TryGetObjectField(PromptId, PromptObj))
		{
			return false;
		}

		FString ErrorField;
		if ((*PromptObj)->TryGetStringField(TEXT("error"), ErrorField) && !ErrorField.IsEmpty())
		{
			OutError = ErrorField;
			return true;
		}

		const TSharedPtr<FJsonObject>* StatusObj = nullptr;
		if ((*PromptObj)->TryGetObjectField(TEXT("status"), StatusObj))
		{
			FString StatusStr;
			if ((*StatusObj)->TryGetStringField(TEXT("status_str"), StatusStr))
			{
				FString StatusLower = StatusStr;
				StatusLower.ToLowerInline();
				if (StatusLower.Contains(TEXT("error")) || StatusLower.Contains(TEXT("fail")))
				{
					FString Details;
					const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
					if ((*StatusObj)->TryGetArrayField(TEXT("messages"), Messages))
					{
						TArray<FString> Parts;
						for (const TSharedPtr<FJsonValue>& MsgVal : *Messages)
						{
							AppendJsonStringValues(MsgVal, Parts);
						}
						if (Parts.Num() > 0)
						{
							Details = FString::Join(Parts, TEXT(" | "));
						}
					}

					OutError = Details.IsEmpty()
						? FString::Printf(TEXT("ComfyUI status: %s"), *StatusStr)
						: FString::Printf(TEXT("ComfyUI status: %s (%s)"), *StatusStr, *Details);
					return true;
				}
			}
		}

		return false;
	}
}

FComfyUIClient::FComfyUIClient(const UChordPBRSettings& InSettings)
{
	BaseUrl = NormalizeBaseUrl(InSettings.ComfyHttpBaseUrl);
	RequestTimeoutSeconds = InSettings.RequestTimeoutSeconds;
	bUseWebSocket = InSettings.bUseWebSocketProgress;
	PollingIntervalSeconds = InSettings.PollingFallbackIntervalSeconds;
}

bool FComfyUIClient::ExecuteJsonRequestBlocking(const FString& Url, const FString& Verb, const FString& ContentType, const FString& Body, FHttpResponsePtr& OutResponse, FString& OutError) const
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Content-Type"), ContentType);
	if (!Body.IsEmpty())
	{
		Request->SetContentAsString(Body);
	}

	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
	Request->OnProcessRequestComplete().BindLambda([&OutResponse, CompletionEvent](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess)
		{
			OutResponse = Response;
		}
		CompletionEvent->Trigger();
	});

	if (!Request->ProcessRequest())
	{
		OutError = FString::Printf(TEXT("Failed to start HTTP request: %s"), *Url);
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
		return false;
	}

	CompletionEvent->Wait(static_cast<uint32>(RequestTimeoutSeconds * 1000.0f));
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

	if (!OutResponse.IsValid())
	{
		OutError = FString::Printf(TEXT("Request timed out or failed: %s"), *Url);
		return false;
	}

	return true;
}

bool FComfyUIClient::ExecuteBinaryRequestBlocking(const FString& Url, const FString& Verb, const TArray<uint8>& Body, FHttpResponsePtr& OutResponse, FString& OutError, const FString& ContentType) const
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Content-Type"), ContentType);
	Request->SetContent(Body);

	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
	Request->OnProcessRequestComplete().BindLambda([&OutResponse, CompletionEvent](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
	{
		if (bSuccess)
		{
			OutResponse = Response;
		}
		CompletionEvent->Trigger();
	});

	if (!Request->ProcessRequest())
	{
		OutError = FString::Printf(TEXT("Failed to start HTTP request: %s"), *Url);
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
		return false;
	}

	CompletionEvent->Wait(static_cast<uint32>(RequestTimeoutSeconds * 1000.0f));
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

	if (!OutResponse.IsValid())
	{
		OutError = FString::Printf(TEXT("Request timed out or failed: %s"), *Url);
		return false;
	}

	return true;
}

bool FComfyUIClient::HealthCheck(FString& OutError) const
{
	FHttpResponsePtr Response;
	if (!ExecuteJsonRequestBlocking(BaseUrl + TEXT("/system_stats"), TEXT("GET"), TEXT("application/json"), TEXT(""), Response, OutError))
	{
		return false;
	}

	if (Response->GetResponseCode() != 200)
	{
		OutError = FString::Printf(TEXT("System stats failed (%d)"), Response->GetResponseCode());
		return false;
	}

	return true;
}

bool FComfyUIClient::QueuePrompt(const TSharedPtr<FJsonObject>& PromptObject, FComfyPromptResponse& OutResponse, FString& OutError) const
{
	if (!PromptObject.IsValid())
	{
		OutError = TEXT("Invalid prompt JSON.");
		return false;
	}

	const FString ClientId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetObjectField(TEXT("prompt"), PromptObject);
	Payload->SetStringField(TEXT("client_id"), ClientId);

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

	FHttpResponsePtr Response;
	if (!ExecuteJsonRequestBlocking(BaseUrl + TEXT("/prompt"), TEXT("POST"), TEXT("application/json"), Body, Response, OutError))
	{
		return false;
	}

	if (Response->GetResponseCode() != 200)
	{
		OutError = FString::Printf(TEXT("Queue prompt failed (%d)"), Response->GetResponseCode());
		return false;
	}

	TSharedPtr<FJsonObject> ResponseObj;
	if (!ParseJsonResponse(Response, ResponseObj, OutError))
	{
		return false;
	}

	FString ErrorField;
	if (ResponseObj->TryGetStringField(TEXT("error"), ErrorField) && !ErrorField.IsEmpty())
	{
		OutError = ErrorField;
		return false;
	}

	const TSharedPtr<FJsonObject>* NodeErrorsObj = nullptr;
	if (ResponseObj->TryGetObjectField(TEXT("node_errors"), NodeErrorsObj) && (*NodeErrorsObj)->Values.Num() > 0)
	{
		TArray<FString> NodeErrorMessages;
		for (const auto& Pair : (*NodeErrorsObj)->Values)
		{
			TArray<FString> Parts;
			AppendJsonStringValues(Pair.Value, Parts);
			const FString Summary = Parts.Num() > 0 ? FString::Join(Parts, TEXT(" | ")) : TEXT("Unknown error");
			NodeErrorMessages.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Summary));
		}

		OutError = FString::Printf(TEXT("Prompt node errors: %s"), *FString::Join(NodeErrorMessages, TEXT("; ")));
		return false;
	}

	OutResponse.ClientId = ClientId;
	OutResponse.PromptId = ResponseObj->GetStringField(TEXT("prompt_id"));
	return true;
}

bool FComfyUIClient::WaitOnWebSocket(const FString& PromptId, const FString& ClientId, FString& OutError, TFunction<void(float)> OnProgress) const
{
	FString WsUrl = BaseUrl.Replace(TEXT("https://"), TEXT("wss://")).Replace(TEXT("http://"), TEXT("ws://"));
	WsUrl += FString::Printf(TEXT("/ws?clientId=%s"), *ClientId);

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
	{
		FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	}

	TSharedRef<FEvent, ESPMode::ThreadSafe> CompletionEventRef =
		MakeShareable(FPlatformProcess::GetSynchEventFromPool(true), [](FEvent* Event)
		{
			FPlatformProcess::ReturnSynchEventToPool(Event);
		});

	TSharedPtr<FWebSocketWaitState, ESPMode::ThreadSafe> State = MakeShared<FWebSocketWaitState, ESPMode::ThreadSafe>(CompletionEventRef);
	TSharedPtr<IWebSocket> Socket = FWebSocketsModule::Get().CreateWebSocket(WsUrl);

	Socket->OnMessage().AddLambda([State, PromptId, OnProgress](const FString& Message)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			FString Type;
			if (Obj->TryGetStringField(TEXT("type"), Type))
			{
				if (Type == TEXT("execution_error"))
				{
					const TSharedPtr<FJsonObject>* DataObj = nullptr;
					if (Obj->TryGetObjectField(TEXT("data"), DataObj))
					{
						FString DataPromptId;
						(*DataObj)->TryGetStringField(TEXT("prompt_id"), DataPromptId);
						if (DataPromptId == PromptId)
						{
							FString ExceptionMessage;
							FString NodeType;
							FString NodeId;
							(*DataObj)->TryGetStringField(TEXT("exception_message"), ExceptionMessage);
							(*DataObj)->TryGetStringField(TEXT("node_type"), NodeType);
							(*DataObj)->TryGetStringField(TEXT("node_id"), NodeId);

							FString LocalError;
							if (!NodeType.IsEmpty() || !NodeId.IsEmpty())
							{
								const FString NodeLabel = NodeId.IsEmpty()
									? NodeType
									: (NodeType.IsEmpty() ? NodeId : FString::Printf(TEXT("%s %s"), *NodeType, *NodeId));
								LocalError = ExceptionMessage.IsEmpty()
									? FString::Printf(TEXT("Execution error (%s)."), *NodeLabel)
									: FString::Printf(TEXT("Execution error (%s): %s"), *NodeLabel, *ExceptionMessage);
							}
							else
							{
								LocalError = ExceptionMessage.IsEmpty()
									? TEXT("Execution error.")
									: FString::Printf(TEXT("Execution error: %s"), *ExceptionMessage);
							}

							{
								FScopeLock Lock(&State->Mutex);
								State->Error = LocalError;
							}
							State->bFailed.Store(true);
							State->CompletionEvent->Trigger();
							return;
						}
					}
				}
				else if (Type == TEXT("executing"))
				{
					const TSharedPtr<FJsonObject>* DataObj = nullptr;
					if (Obj->TryGetObjectField(TEXT("data"), DataObj))
					{
						FString DataPromptId = (*DataObj)->GetStringField(TEXT("prompt_id"));
						bool bNodeNull = !(*DataObj)->HasTypedField<EJson::String>(TEXT("node")) || (*DataObj)->GetStringField(TEXT("node")).IsEmpty();
						if (DataPromptId == PromptId && bNodeNull)
						{
							State->bDone.Store(true);
							State->CompletionEvent->Trigger();
						}
					}
				}
				else if (Type == TEXT("progress") && OnProgress)
				{
					const TSharedPtr<FJsonObject>* DataObj = nullptr;
					if (Obj->TryGetObjectField(TEXT("data"), DataObj))
					{
						double Value = 0;
						double Max = 0;
						if ((*DataObj)->TryGetNumberField(TEXT("value"), Value) && (*DataObj)->TryGetNumberField(TEXT("max"), Max))
						{
							if (Max > 0)
							{
								float Progress = static_cast<float>(Value / Max);
								AsyncTask(ENamedThreads::GameThread, [OnProgress, Progress]()
								{
									OnProgress(Progress);
								});
							}
						}
					}
				}
			}
		}
	});

	Socket->OnConnectionError().AddLambda([State](const FString& Error)
	{
		{
			FScopeLock Lock(&State->Mutex);
			State->Error = FString::Printf(TEXT("WebSocket error: %s"), *Error);
		}
		State->bFailed.Store(true);
		State->CompletionEvent->Trigger();
	});

	Socket->OnClosed().AddLambda([State](int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		if (State->bDone.Load() || State->bFailed.Load())
		{
			return;
		}
		{
			FScopeLock Lock(&State->Mutex);
			State->Error = Reason.IsEmpty()
				? FString::Printf(TEXT("WebSocket closed (%d)."), StatusCode)
				: FString::Printf(TEXT("WebSocket closed (%d): %s"), StatusCode, *Reason);
		}
		State->bFailed.Store(true);
		State->CompletionEvent->Trigger();
	});

	Socket->Connect();

	State->CompletionEvent->Wait(static_cast<uint32>(RequestTimeoutSeconds * 1000.0f));
	Socket->OnMessage().Clear();
	Socket->OnConnectionError().Clear();
	Socket->OnClosed().Clear();
	Socket->Close();

	if (State->bDone.Load())
	{
		return true;
	}

	if (State->bFailed.Load())
	{
		FScopeLock Lock(&State->Mutex);
		OutError = State->Error.IsEmpty() ? TEXT("WebSocket failed.") : State->Error;
		return false;
	}

	if (!State->bFailed.Load())
	{
		OutError = TEXT("WebSocket wait timed out.");
	}

	return false;
}

bool FComfyUIClient::PollHistoryUntilComplete(const FString& PromptId, TSharedPtr<FJsonObject>& OutHistory, FString& OutError) const
{
	const double StartTime = FPlatformTime::Seconds();

	while (FPlatformTime::Seconds() - StartTime < RequestTimeoutSeconds)
	{
		if (GetHistory(PromptId, OutHistory, OutError) && OutHistory.IsValid())
		{
			if (TryExtractHistoryError(OutHistory, PromptId, OutError))
			{
				return false;
			}

			const TSharedPtr<FJsonObject>* PromptObj = nullptr;
			if (OutHistory->TryGetObjectField(PromptId, PromptObj))
			{
				const TSharedPtr<FJsonObject>* OutputsObj = nullptr;
				if ((*PromptObj)->TryGetObjectField(TEXT("outputs"), OutputsObj) && (*OutputsObj)->Values.Num() > 0)
				{
					return true;
				}
			}
		}

		FPlatformProcess::Sleep(PollingIntervalSeconds);
	}

	OutError = TEXT("Polling history timed out.");
	return false;
}

bool FComfyUIClient::WaitForCompletion(const FString& PromptId, const FString& ClientId, TSharedPtr<FJsonObject>& OutHistory, FString& OutError, TFunction<void(float)> OnProgress) const
{
	bool bSocketOk = false;
	if (bUseWebSocket)
	{
		bSocketOk = WaitOnWebSocket(PromptId, ClientId, OutError, OnProgress);
		if (!bSocketOk && OutError.StartsWith(TEXT("Execution error")))
		{
			return false;
		}
	}

	if (!bSocketOk)
	{
		return PollHistoryUntilComplete(PromptId, OutHistory, OutError);
	}

	// If websocket completed, fetch history once.
	return GetHistory(PromptId, OutHistory, OutError);
}

bool FComfyUIClient::GetHistory(const FString& PromptId, TSharedPtr<FJsonObject>& OutHistory, FString& OutError) const
{
	FHttpResponsePtr Response;
	if (!ExecuteJsonRequestBlocking(BaseUrl + TEXT("/history/") + PromptId, TEXT("GET"), TEXT("application/json"), TEXT(""), Response, OutError))
	{
		return false;
	}

	if (Response->GetResponseCode() != 200)
	{
		OutError = FString::Printf(TEXT("History failed (%d)"), Response->GetResponseCode());
		return false;
	}

	return ParseJsonResponse(Response, OutHistory, OutError);
}

bool FComfyUIClient::ParseImageOutputs(const TSharedPtr<FJsonObject>& History, TArray<FComfyImageReference>& OutImages) const
{
	if (!History.IsValid())
	{
		return false;
	}

	for (const auto& PromptKV : History->Values)
	{
		const TSharedPtr<FJsonObject>* PromptObj = nullptr;
		if (PromptKV.Value->TryGetObject(PromptObj))
		{
			const TSharedPtr<FJsonObject>* OutputsObj = nullptr;
			if ((*PromptObj)->TryGetObjectField(TEXT("outputs"), OutputsObj))
			{
				for (const auto& OutputKV : (*OutputsObj)->Values)
				{
					const TSharedPtr<FJsonObject>* OutputObj = nullptr;
					if (OutputKV.Value->TryGetObject(OutputObj))
					{
						const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
						if ((*OutputObj)->TryGetArrayField(TEXT("images"), ImagesArray))
						{
							for (const TSharedPtr<FJsonValue>& Val : *ImagesArray)
							{
								const TSharedPtr<FJsonObject>* ImgObj = nullptr;
								if (Val->TryGetObject(ImgObj))
								{
									FComfyImageReference Ref;
									(*ImgObj)->TryGetStringField(TEXT("filename"), Ref.Filename);
									(*ImgObj)->TryGetStringField(TEXT("subfolder"), Ref.Subfolder);
									(*ImgObj)->TryGetStringField(TEXT("type"), Ref.Type);
									if (!Ref.Filename.IsEmpty())
									{
										OutImages.Add(Ref);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return OutImages.Num() > 0;
}

bool FComfyUIClient::DownloadImage(const FComfyImageReference& Ref, TArray<uint8>& OutData, FString& OutError) const
{
	FString Url = BaseUrl + TEXT("/view?filename=") + FGenericPlatformHttp::UrlEncode(Ref.Filename);
	if (!Ref.Subfolder.IsEmpty())
	{
		Url += TEXT("&subfolder=") + FGenericPlatformHttp::UrlEncode(Ref.Subfolder);
	}
	if (!Ref.Type.IsEmpty())
	{
		Url += TEXT("&type=") + FGenericPlatformHttp::UrlEncode(Ref.Type);
	}

	FHttpResponsePtr Response;
	if (!ExecuteBinaryRequestBlocking(Url, TEXT("GET"), TArray<uint8>(), Response, OutError))
	{
		return false;
	}

	if (Response->GetResponseCode() != 200)
	{
		OutError = FString::Printf(TEXT("Download failed (%d) %s"), Response->GetResponseCode(), *Ref.Filename);
		return false;
	}

	OutData = Response->GetContent();
	return true;
}

bool FComfyUIClient::UploadImage(const TArray<uint8>& ImageData, const FString& FileName, FComfyImageReference& OutRef, FString& OutError) const
{
	const FString Boundary = TEXT("----ChordPBRGeneratorBoundary");
	TArray<uint8> Body;

	auto AppendString = [&Body](const FString& Str)
	{
		auto Ansi = StringCast<ANSICHAR>(*Str);
		Body.Append(reinterpret_cast<const uint8*>(Ansi.Get()), Ansi.Length());
	};

	AppendString(TEXT("--") + Boundary + TEXT("\r\n"));
	AppendString(TEXT("Content-Disposition: form-data; name=\"image\"; filename=\"") + FileName + TEXT("\"\r\n"));
	AppendString(TEXT("Content-Type: application/octet-stream\r\n\r\n"));
	Body.Append(ImageData);
	AppendString(TEXT("\r\n--") + Boundary + TEXT("--\r\n"));

	FHttpResponsePtr Response;
	if (!ExecuteBinaryRequestBlocking(BaseUrl + TEXT("/upload/image"), TEXT("POST"), Body, Response, OutError, FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary)))
	{
		return false;
	}

	if (Response->GetResponseCode() != 200)
	{
		OutError = FString::Printf(TEXT("Upload failed (%d)"), Response->GetResponseCode());
		return false;
	}

	TSharedPtr<FJsonObject> Obj;
	if (!ParseJsonResponse(Response, Obj, OutError))
	{
		return false;
	}

	Obj->TryGetStringField(TEXT("name"), OutRef.Filename);
	OutRef.Subfolder = Obj->GetStringField(TEXT("subfolder"));
	OutRef.Type = Obj->GetStringField(TEXT("type"));

	return !OutRef.Filename.IsEmpty();
}

bool FComfyUIClient::Cancel(FString& OutError) const
{
	FHttpResponsePtr Response;
	if (!ExecuteJsonRequestBlocking(BaseUrl + TEXT("/interrupt"), TEXT("POST"), TEXT("application/json"), TEXT("{}"), Response, OutError))
	{
		return false;
	}

	return Response.IsValid() && Response->GetResponseCode() == 200;
}
