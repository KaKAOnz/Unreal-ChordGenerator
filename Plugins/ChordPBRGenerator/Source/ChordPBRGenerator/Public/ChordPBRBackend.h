// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FChordPBRSession;

class FChordPBRBackend
{
public:
	static void GenerateImagesFromPrompt(const FString& Prompt, FChordPBRSession& Session);
	static void GeneratePBRFromImage(class UTexture2D* SourceImage, FChordPBRSession& Session);
};
