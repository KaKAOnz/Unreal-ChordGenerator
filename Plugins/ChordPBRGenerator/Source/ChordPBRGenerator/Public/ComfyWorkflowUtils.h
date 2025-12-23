// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChordPBRSettings.h"
#include "ComfyUIClient.h"

class FJsonObject;

namespace FComfyWorkflowUtils
{
	bool LoadPromptTemplate(const FString& Path, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError);

	bool PatchTxt2ImgPrompt(const UChordPBRSettings& Settings, const FString& Prompt, int32 Seed, const FString& FilenamePrefix, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError);

	bool PatchChordPrompt(const UChordPBRSettings& Settings, const FComfyImageReference& UploadedImage, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError);

	bool ExtractImagesFromHistory(const UChordPBRSettings& Settings, const TSharedPtr<FJsonObject>& History, TArray<FComfyImageReference>& OutImages, FString& OutError);
	bool ExtractPBRFromHistory(const UChordPBRSettings& Settings, const TSharedPtr<FJsonObject>& History, TMap<FString, FComfyImageReference>& OutChannels, FString& OutError);
}
