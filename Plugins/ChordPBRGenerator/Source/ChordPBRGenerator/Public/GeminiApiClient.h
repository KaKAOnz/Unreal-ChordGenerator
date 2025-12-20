// Copyright ChordPBRGenerator

#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_TwoParams(FOnGeminiImageGenerated, UTexture2D* /*GeneratedTexture*/, const FString& /*Error*/);

/**
 * Client for Gemini API image generation
 */
class FGeminiApiClient : public TSharedFromThis<FGeminiApiClient>
{
public:
	FGeminiApiClient() = default;
	~FGeminiApiClient() = default;

	/**
	 * Generate an image from a text prompt using Gemini API
	 * @param ApiEndpoint The Gemini API endpoint URL
	 * @param ApiKey The Gemini API key
	 * @param Model The Gemini model name (e.g., gemini-2.5-flash-image)
	 * @param Prompt The text prompt for image generation
	 * @param OnComplete Callback when generation completes
	 */
	void GenerateImageAsync(
		const FString& ApiEndpoint,
		const FString& ApiKey,
		const FString& Model,
		const FString& Prompt,
		FOnGeminiImageGenerated OnComplete);

	/** Cancel any pending request */
	void CancelRequest();

	/** Check if a request is in progress */
	bool IsRequestInProgress() const { return bIsRequestInProgress; }

private:
	bool bIsRequestInProgress = false;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> PendingRequest;
};
