// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Materials/MaterialInterface.h"
#include "ChordPBRSettings.generated.h"

// Text-to-Image backend selection
UENUM()
enum class ETxt2ImgBackend : uint8
{
	ComfyUI UMETA(DisplayName = "Local ComfyUI"),
	GeminiAPI UMETA(DisplayName = "Gemini API")
};

USTRUCT()
struct FComfyTxt2ImgBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString PromptNodeIdentifier;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString PromptInputName = TEXT("text");

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString SeedNodeIdentifier;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString SeedInputName = TEXT("seed");
};

USTRUCT()
struct FComfyPBRChannelBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	int32 NodeId = -1;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString OutputName = TEXT("images");

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString FilenameHintContains;
};

USTRUCT()
struct FComfyChordBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	int32 LoadImageNodeId = -1;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FString LoadImageInputName = TEXT("image");

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FComfyPBRChannelBinding BaseColor;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FComfyPBRChannelBinding Normal;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FComfyPBRChannelBinding Roughness;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FComfyPBRChannelBinding Metallic;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FComfyPBRChannelBinding Height;
};

UCLASS(Config = Editor, DefaultConfig)
class UChordPBRSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UChordPBRSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ========== Text-to-Image Generation ==========
	
	UPROPERTY(EditAnywhere, Config, Category = "Text-to-Image", meta = (DisplayName = "Image Generation Backend"))
	ETxt2ImgBackend Txt2ImgBackend = ETxt2ImgBackend::ComfyUI;

	// ComfyUI Workflow Settings for Text-to-Image (only shown when ComfyUI is selected)
	UPROPERTY(EditAnywhere, Config, Category = "Text-to-Image", meta = (EditCondition = "Txt2ImgBackend == ETxt2ImgBackend::ComfyUI", EditConditionHides, ToolTip = "Txt2Img API prompt template path (JSON). Plugin-relative or absolute."))
	FString Txt2ImgApiPromptPath;

	UPROPERTY(EditAnywhere, Config, Category = "Text-to-Image", meta = (EditCondition = "Txt2ImgBackend == ETxt2ImgBackend::ComfyUI", EditConditionHides))
	FComfyTxt2ImgBinding Txt2ImgBinding;

	// Gemini API Settings for Text-to-Image (only shown when Gemini API is selected)
	UPROPERTY(EditAnywhere, Config, Category = "Text-to-Image", meta = (EditCondition = "Txt2ImgBackend == ETxt2ImgBackend::GeminiAPI", EditConditionHides, DisplayName = "Gemini API Key", PasswordField = true, ToolTip = "Your Gemini API key from Google AI Studio."))
	FString GeminiApiKey;

	UPROPERTY(EditAnywhere, Config, Category = "Text-to-Image", meta = (EditCondition = "Txt2ImgBackend == ETxt2ImgBackend::GeminiAPI", EditConditionHides, DisplayName = "Gemini Model", ToolTip = "Gemini model to use for image generation."))
	FString GeminiModel = TEXT("gemini-2.5-flash-image");

	UPROPERTY(EditAnywhere, Config, Category = "Text-to-Image", meta = (EditCondition = "Txt2ImgBackend == ETxt2ImgBackend::GeminiAPI", EditConditionHides, DisplayName = "Gemini API Endpoint", ToolTip = "Gemini API endpoint URL."))
	FString GeminiApiEndpoint = TEXT("https://generativelanguage.googleapis.com/v1beta/models");

	// ========== PBR Map Generation (Always uses ComfyUI) ==========
	
	UPROPERTY(EditAnywhere, Config, Category = "PBR Generation", meta = (DisplayName = "ComfyUI HTTP Base URL", ToolTip = "ComfyUI server address for PBR generation."))
	FString ComfyHttpBaseUrl;

	UPROPERTY(EditAnywhere, Config, Category = "PBR Generation", meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds;

	UPROPERTY(EditAnywhere, Config, Category = "PBR Generation")
	bool bUseWebSocketProgress;

	UPROPERTY(EditAnywhere, Config, Category = "PBR Generation", meta = (ClampMin = "0.05"))
	float PollingFallbackIntervalSeconds;
	
	UPROPERTY(EditAnywhere, Config, Category = "PBR Generation", meta = (ToolTip = "CHORD image-to-PBR API prompt template path (JSON). User-provided."))
	FString ChordImg2PbrApiPromptPath;

	UPROPERTY(EditAnywhere, Config, Category = "PBR Generation")
	FComfyChordBinding ChordBinding;

	// ========== General Settings ==========
	
	UPROPERTY(EditAnywhere, Config, Category = "General", meta = (ToolTip = "Cache root. Allowed under Saved/ChordPBRGenerator only."))
	FString SavedCacheRoot;

	UPROPERTY(EditAnywhere, Config, Category = "General", meta = (ToolTip = "Preview master material asset for transient PBR previews."))
	FSoftObjectPath PreviewMasterMaterial;

	UPROPERTY(EditAnywhere, Config, Category = "General", meta = (ToolTip = "Default root save path for textures/material instances (e.g. /Game/ChordPBR)."))
	FString DefaultSaveRootPath;

	UPROPERTY(EditAnywhere, Config, Category = "General", meta = (ToolTip = "Master material parent for saved material instances."))
	TSoftObjectPtr<UMaterialInterface> InstanceMasterMaterial;

	// Legacy property for backwards compatibility
	bool bUseGeminiApi = false;
};
