// Copyright 2025 KaKAOnz. All Rights Reserved.

#include "ChordImageUtils.h"

#include "Runtime/Launch/Resources/Version.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

namespace
{
	UTexture2D* CreateTextureFromRaw(const TArray64<uint8>& RawData, int32 Width, int32 Height, const FString& DebugName)
	{
		if (Width <= 0 || Height <= 0 || RawData.Num() == 0)
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}

#if WITH_EDITORONLY_DATA
		Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_HDR;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		const int64 BufferSize = static_cast<int64>(Width) * static_cast<int64>(Height) * sizeof(FColor);
		if (BufferSize <= RawData.Num())
		{
			FMemory::Memcpy(Data, RawData.GetData(), BufferSize);
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();

		if (!DebugName.IsEmpty())
		{
			Texture->Rename(*DebugName);
		}

		return Texture;
	}
}

UTexture2D* FChordImageUtils::CreateTextureFromImage(const TArray<uint8>& ImageData, const FString& DebugName)
{
	if (ImageData.Num() == 0)
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TArray<EImageFormat> Formats = { EImageFormat::PNG, EImageFormat::JPEG, EImageFormat::BMP, EImageFormat::EXR };

	for (EImageFormat Format : Formats)
	{
		TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
		if (Wrapper.IsValid() && Wrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
		{
			TArray64<uint8> RawData;
			if (Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
			{
				return CreateTextureFromRaw(RawData, Wrapper->GetWidth(), Wrapper->GetHeight(), DebugName);
			}
		}
	}

	return nullptr;
}

bool FChordImageUtils::EncodeTextureToPng(UTexture2D* Texture, TArray<uint8>& OutPngData, FString& OutError)
{
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		OutError = TEXT("Invalid texture.");
		return false;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	const int32 Width = Texture->GetSizeX();
	const int32 Height = Texture->GetSizeY();

	TArray<FColor> SrcData;
	SrcData.SetNumUninitialized(static_cast<int64>(Width) * static_cast<int64>(Height));
	void* Data = Mip.BulkData.Lock(LOCK_READ_ONLY);
	FMemory::Memcpy(SrcData.GetData(), Data, SrcData.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	// UE 5.5+ requires TArray64 for output and uses PNGCompressImageArray
	TArray64<uint8> TempPngData;
	FImageUtils::PNGCompressImageArray(Width, Height, SrcData, TempPngData);
	OutPngData.Empty(TempPngData.Num());
	OutPngData.Append(TempPngData.GetData(), TempPngData.Num());
#else
	// UE 5.4 and earlier uses CompressImageArray with regular TArray
	FImageUtils::CompressImageArray(Width, Height, SrcData, OutPngData);
#endif
	if (OutPngData.Num() == 0)
	{
		OutError = TEXT("PNG compression failed.");
		return false;
	}

	return true;
}

bool FChordImageUtils::SaveTextureToPng(UTexture2D* Texture, const FString& AbsoluteFilePath, FString& OutError)
{
	TArray<uint8> PngData;
	if (!EncodeTextureToPng(Texture, PngData, OutError))
	{
		return false;
	}

	if (!FFileHelper::SaveArrayToFile(PngData, *AbsoluteFilePath))
	{
		OutError = FString::Printf(TEXT("Failed to write %s"), *AbsoluteFilePath);
		return false;
	}

	return true;
}
