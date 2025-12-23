// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChordPBRSettings.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Http.h"

class FJsonObject;

struct FComfyImageReference
{
	FString Filename;
	FString Subfolder;
	FString Type;
};

struct FComfyPromptResponse
{
	FString PromptId;
	FString ClientId;
};

class FComfyUIClient
{
public:
	explicit FComfyUIClient(const UChordPBRSettings& InSettings);

	bool HealthCheck(FString& OutError) const;
	bool QueuePrompt(const TSharedPtr<FJsonObject>& PromptObject, FComfyPromptResponse& OutResponse, FString& OutError) const;
	bool WaitForCompletion(const FString& PromptId, const FString& ClientId, TSharedPtr<FJsonObject>& OutHistory, FString& OutError, TFunction<void(float)> OnProgress = nullptr) const;
	bool GetHistory(const FString& PromptId, TSharedPtr<FJsonObject>& OutHistory, FString& OutError) const;
	bool DownloadImage(const FComfyImageReference& Ref, TArray<uint8>& OutData, FString& OutError) const;
	bool UploadImage(const TArray<uint8>& ImageData, const FString& FileName, FComfyImageReference& OutRef, FString& OutError) const;
	bool Cancel(FString& OutError) const;

private:
	bool ExecuteJsonRequestBlocking(const FString& Url, const FString& Verb, const FString& ContentType, const FString& Body, FHttpResponsePtr& OutResponse, FString& OutError) const;
	bool ExecuteBinaryRequestBlocking(const FString& Url, const FString& Verb, const TArray<uint8>& Body, FHttpResponsePtr& OutResponse, FString& OutError, const FString& ContentType = TEXT("application/octet-stream")) const;
	bool WaitOnWebSocket(const FString& PromptId, const FString& ClientId, FString& OutError, TFunction<void(float)> OnProgress = nullptr) const;
	bool PollHistoryUntilComplete(const FString& PromptId, TSharedPtr<FJsonObject>& OutHistory, FString& OutError) const;
	bool ParseImageOutputs(const TSharedPtr<FJsonObject>& History, TArray<FComfyImageReference>& OutImages) const;

private:
	FString BaseUrl;
	float RequestTimeoutSeconds = 300.0f;
	bool bUseWebSocket = true;
	float PollingIntervalSeconds = 0.5f;
};
