// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

enum class EChordGalleryLayer : uint8
{
	Root,
	Detail
};

struct FChordPBRMapSet
{
	FName Label;
	TWeakObjectPtr<UTexture2D> SourceImage;
	TStrongObjectPtr<UTexture2D> BaseColor;
	TStrongObjectPtr<UTexture2D> Normal;
	TStrongObjectPtr<UTexture2D> Roughness;
	TStrongObjectPtr<UTexture2D> Metallic;
	TStrongObjectPtr<UTexture2D> Height;
	FString BaseColorPath;
	FString NormalPath;
	FString RoughnessPath;
	FString MetallicPath;
	FString HeightPath;
};

struct FChordGeneratedImageItem
{
	TStrongObjectPtr<UTexture2D> Image;
	FString Label;
	bool bHasPBR = false;
	FChordPBRMapSet PBRMaps;
	TStrongObjectPtr<UMaterialInstanceDynamic> PreviewMID;
};

class FChordPBRSession
{
public:
	void Reset();

	const TArray<FChordGeneratedImageItem>& GetGeneratedImages() const { return GeneratedImages; }

	int32 AddGeneratedImage(UTexture2D* Texture, const FString& Label = FString());
	bool RemoveGeneratedImage(int32 ImageIndex);
	bool SetPBRMapsForImage(int32 ImageIndex, FChordPBRMapSet&& MapSet);
	bool HasPBRForImage(int32 ImageIndex) const;
	const FChordPBRMapSet* GetPBRMapsForImage(int32 ImageIndex) const;
	FChordGeneratedImageItem* GetMutableImageItem(int32 ImageIndex);

private:
	TArray<FChordGeneratedImageItem> GeneratedImages;
};
