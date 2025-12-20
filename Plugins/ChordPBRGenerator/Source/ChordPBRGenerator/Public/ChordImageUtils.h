// Copyright ChordPBRGenerator

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

namespace FChordImageUtils
{
	// Decode image bytes (PNG/JPG/WebP) into a transient texture.
	UTexture2D* CreateTextureFromImage(const TArray<uint8>& ImageData, const FString& DebugName);

	// Encode a transient texture's first mip to PNG bytes.
	bool EncodeTextureToPng(UTexture2D* Texture, TArray<uint8>& OutPngData, FString& OutError);

	// Encode a transient texture and write it to disk as PNG.
	bool SaveTextureToPng(UTexture2D* Texture, const FString& AbsoluteFilePath, FString& OutError);
}
