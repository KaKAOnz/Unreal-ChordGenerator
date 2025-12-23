// Copyright 2025 KaKAOnz. All Rights Reserved.

#include "ChordTextureGenerator.h"

#include "Engine/Texture2D.h"
#include "Rendering/Texture2DResource.h"
#include "UObject/Package.h"

UTexture2D* FChordTextureGenerator::CreateTextureInternal(int32 Size, const TFunctionRef<FColor(int32 X, int32 Y)>& PixelGenerator)
{
	if (Size <= 0)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
	Texture->CompressionSettings = TC_HDR;
	Texture->SRGB = true;

	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		return nullptr;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);

	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			Pixels[Y * Size + X] = PixelGenerator(X, Y);
		}
	}

	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	const int64 BufferSize = Pixels.Num() * sizeof(FColor);
	FMemory::Memcpy(Data, Pixels.GetData(), BufferSize);
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

UTexture2D* FChordTextureGenerator::CreateCheckerTexture(int32 Size, const FLinearColor& ColorA, const FLinearColor& ColorB, int32 SquaresPerSide)
{
	SquaresPerSide = FMath::Max(1, SquaresPerSide);
	const int32 CellSize = FMath::Max(1, Size / SquaresPerSide);

	return CreateTextureInternal(Size, [ColorA, ColorB, CellSize](int32 X, int32 Y)
	{
		const int32 CellX = X / CellSize;
		const int32 CellY = Y / CellSize;
		const bool bUseA = ((CellX + CellY) % 2) == 0;
		return (bUseA ? ColorA : ColorB).ToFColor(true);
	});
}

UTexture2D* FChordTextureGenerator::CreateLinearGradientTexture(int32 Size, const FLinearColor& StartColor, const FLinearColor& EndColor, bool bHorizontal)
{
	return CreateTextureInternal(Size, [StartColor, EndColor, bHorizontal, Size](int32 X, int32 Y)
	{
		const float T = bHorizontal ? (static_cast<float>(X) / static_cast<float>(Size - 1)) : (static_cast<float>(Y) / static_cast<float>(Size - 1));
		return FLinearColor::LerpUsingHSV(StartColor, EndColor, T).ToFColor(true);
	});
}

UTexture2D* FChordTextureGenerator::CreateRadialFadeTexture(int32 Size, const FLinearColor& CenterColor, const FLinearColor& EdgeColor)
{
	const FVector2D Center(Size * 0.5f, Size * 0.5f);
	const float MaxDistance = Center.Size();

	return CreateTextureInternal(Size, [Center, MaxDistance, CenterColor, EdgeColor](int32 X, int32 Y)
	{
		const float Distance = FVector2D::Distance(FVector2D(X, Y), Center);
		const float T = FMath::Clamp(Distance / MaxDistance, 0.0f, 1.0f);
		return FLinearColor::LerpUsingHSV(CenterColor, EdgeColor, T).ToFColor(true);
	});
}
