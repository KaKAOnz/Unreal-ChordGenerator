// Copyright ChordPBRGenerator

#include "ChordPBRSession.h"
#include "Misc/Paths.h"

int32 FChordPBRSession::AddGeneratedImage(UTexture2D* Texture, const FString& Label)
{
	if (!Texture)
	{
		return GeneratedImages.Num();
	}

	FChordGeneratedImageItem Item;
	Item.Image = TStrongObjectPtr<UTexture2D>(Texture);
	const FString BaseLabel = Label.IsEmpty() ? Texture->GetName() : FPaths::GetBaseFilename(Label);
	const FString SafeLabel = FPaths::MakeValidFileName(BaseLabel);
	Item.Label = SafeLabel.IsEmpty() ? Texture->GetName() : SafeLabel;
	Item.bHasPBR = false;
	GeneratedImages.Add(MoveTemp(Item));
	return GeneratedImages.Num();
}

bool FChordPBRSession::RemoveGeneratedImage(int32 ImageIndex)
{
	if (!GeneratedImages.IsValidIndex(ImageIndex))
	{
		return false;
	}

	GeneratedImages.RemoveAt(ImageIndex);
	return true;
}

bool FChordPBRSession::SetPBRMapsForImage(int32 ImageIndex, FChordPBRMapSet&& MapSet)
{
	if (!GeneratedImages.IsValidIndex(ImageIndex))
	{
		return false;
	}

	FChordGeneratedImageItem& Item = GeneratedImages[ImageIndex];
	Item.PBRMaps = MoveTemp(MapSet);
	Item.bHasPBR = true;
	return true;
}

bool FChordPBRSession::HasPBRForImage(int32 ImageIndex) const
{
	return GeneratedImages.IsValidIndex(ImageIndex) && GeneratedImages[ImageIndex].bHasPBR;
}

const FChordPBRMapSet* FChordPBRSession::GetPBRMapsForImage(int32 ImageIndex) const
{
	if (!GeneratedImages.IsValidIndex(ImageIndex))
	{
		return nullptr;
	}

	const FChordGeneratedImageItem& Item = GeneratedImages[ImageIndex];
	return Item.bHasPBR ? &Item.PBRMaps : nullptr;
}

FChordGeneratedImageItem* FChordPBRSession::GetMutableImageItem(int32 ImageIndex)
{
	return GeneratedImages.IsValidIndex(ImageIndex) ? &GeneratedImages[ImageIndex] : nullptr;
}

void FChordPBRSession::Reset()
{
	GeneratedImages.Empty();
}
