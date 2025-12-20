// Copyright ChordPBRGenerator

#include "ChordPBRBackend.h"

#include "ChordPBRSession.h"
#include "ChordTextureGenerator.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/DateTime.h"

namespace
{
	int32 GetNextSeed()
	{
		static int32 SeedCounter = 1;
		return SeedCounter++;
	}

	FLinearColor MakeColorFromSeed(int32 Seed, float Alpha = 1.0f)
	{
		const float Hue = static_cast<float>((Seed * 53) % 360);
		FLinearColor Color = FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue / 360.0f * 255.0f), 180, 255);
		Color.A = Alpha;
		return Color;
	}
}

void FChordPBRBackend::GenerateImagesFromPrompt(const FString& Prompt, FChordPBRSession& Session)
{
	const int32 BaseSeed = GetNextSeed();

	const FLinearColor ColorA = MakeColorFromSeed(BaseSeed);
	const FLinearColor ColorB = MakeColorFromSeed(BaseSeed + 1);
	const FLinearColor ColorC = MakeColorFromSeed(BaseSeed + 2);

	// Generate only one preview per request to match the UX expectation.
	const int32 Variant = BaseSeed % 3;
	UTexture2D* Generated = nullptr;
	if (Variant == 0)
	{
		Generated = FChordTextureGenerator::CreateCheckerTexture(256, ColorA, ColorB, 8);
	}
	else if (Variant == 1)
	{
		Generated = FChordTextureGenerator::CreateLinearGradientTexture(256, ColorB, ColorC, true);
	}
	else
	{
		Generated = FChordTextureGenerator::CreateRadialFadeTexture(256, ColorC, ColorA);
	}

	if (Generated)
	{
		const FDateTime Now = FDateTime::Now();
		const FString BaseLabel = FString::Printf(TEXT("IMG_%02d%02d%02d%02d%02d"), Now.GetYear() % 100, Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute());
		const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *BaseLabel);
		Generated->Rename(*UniqueName.ToString());
		Session.AddGeneratedImage(Generated, BaseLabel);
	}
}

void FChordPBRBackend::GeneratePBRFromImage(UTexture2D* SourceImage, FChordPBRSession& Session)
{
	const int32 BaseSeed = GetNextSeed();

	FChordPBRMapSet MapSet;
	MapSet.Label = *FString::Printf(TEXT("PBR_%d"), BaseSeed);
	MapSet.SourceImage = SourceImage;

	const FLinearColor BaseColor = MakeColorFromSeed(BaseSeed);
	const FLinearColor NormalColor = MakeColorFromSeed(BaseSeed + 1);
	const FLinearColor RoughnessColor = MakeColorFromSeed(BaseSeed + 2);
	const FLinearColor MetallicColor = MakeColorFromSeed(BaseSeed + 3);
	const FLinearColor HeightColor = MakeColorFromSeed(BaseSeed + 4);

	MapSet.BaseColor = TStrongObjectPtr<UTexture2D>(FChordTextureGenerator::CreateCheckerTexture(256, BaseColor, BaseColor * 0.5f, 6));
	MapSet.Normal = TStrongObjectPtr<UTexture2D>(FChordTextureGenerator::CreateLinearGradientTexture(256, NormalColor, NormalColor.Desaturate(0.3f), false));
	MapSet.Roughness = TStrongObjectPtr<UTexture2D>(FChordTextureGenerator::CreateLinearGradientTexture(256, RoughnessColor, RoughnessColor * 0.2f, true));
	MapSet.Metallic = TStrongObjectPtr<UTexture2D>(FChordTextureGenerator::CreateRadialFadeTexture(256, MetallicColor, FLinearColor::Black));
	MapSet.Height = TStrongObjectPtr<UTexture2D>(FChordTextureGenerator::CreateCheckerTexture(256, HeightColor, HeightColor * 0.3f, 10));

	// Annotate with a hint of the source
	if (SourceImage)
	{
		MapSet.Label = *FString::Printf(TEXT("PBR_%s"), *SourceImage->GetName());
	}

	if (MapSet.BaseColor.IsValid())
	{
		const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *MapSet.Label.ToString());
		MapSet.BaseColor.Get()->Rename(*UniqueName.ToString());
	}

	int32 TargetIndex = INDEX_NONE;
	const TArray<FChordGeneratedImageItem>& Images = Session.GetGeneratedImages();
	for (int32 Index = 0; Index < Images.Num(); ++Index)
	{
		if (Images[Index].Image.Get() == SourceImage)
		{
			TargetIndex = Index;
			break;
		}
	}

	if (TargetIndex != INDEX_NONE)
	{
		Session.SetPBRMapsForImage(TargetIndex, MoveTemp(MapSet));
	}
}
