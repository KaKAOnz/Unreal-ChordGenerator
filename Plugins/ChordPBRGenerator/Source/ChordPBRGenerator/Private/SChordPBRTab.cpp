// Copyright ChordPBRGenerator

#include "SChordPBRTab.h"
#include "Runtime/Launch/Resources/Version.h"

#include "ChordPBRBackend.h"
#include "ChordPBRSettings.h"
#include "ChordPBRGeneratorModule.h"
#include "ChordImageUtils.h"
#include "ComfyUIClient.h"
#include "GeminiApiClient.h"
#include "ComfyWorkflowUtils.h"
#include "ChordPBRSession.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Materials/MaterialInstanceConstant.h"
#include "HAL/FileManager.h"
#include "Editor/UnrealEdEngine.h"
#include "PackageTools.h"
#include "FileHelpers.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "Styling/SlateBrush.h"
#include "DesktopPlatformModule.h"

namespace
{
	static void ConfigurePBRTexture(UTexture2D* Texture, const FString& Channel)
	{
		if (!Texture)
		{
			return;
		}

		if (Channel == TEXT("Normal"))
		{
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Normalmap;
		}
		else if (Channel == TEXT("BaseColor"))
		{
			Texture->SRGB = true;
			Texture->CompressionSettings = TC_Default;
		}
		else if (Channel == TEXT("Height"))
		{
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Grayscale;
		}
		else
		{
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Masks;
		}

		Texture->PostEditChange();
		Texture->UpdateResource();
	}
}

void SChordPBRTab::Construct(const FArguments& InArgs)
{
	Session = MakeShared<FChordPBRSession>();
	MainImageBrush = MakeShared<FSlateBrush>();
	ComfyClient = MakeShared<FComfyUIClient>(*GetDefault<UChordPBRSettings>());
	StatusMessage = TEXT("Idle");

	ChildSlot
	[
		SNew(SSplitter)
		.PhysicalSplitterHandleSize(4.0f)
		+ SSplitter::Slot()
		.Value(0.35f)
		[
			BuildChat()
		]
		+ SSplitter::Slot()
		.Value(0.65f)
		[
			BuildGallery()
		]
	];

	RebuildThumbnails();
}

FReply SChordPBRTab::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		return OnDeleteCurrentImage();
	}

	return FReply::Unhandled();
}

SChordPBRTab::~SChordPBRTab()
{
	RestorePreviewTarget(true);
}
TSharedRef<SWidget> SChordPBRTab::BuildChat()
{
	return SNew(SBorder)
		.Padding(8.0f)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SWrapBox)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
			.UseAllottedWidth(true)
#endif
			.InnerSlotPadding(FVector2D(6.0f, 2.0f))
			+ SWrapBox::Slot()
			[
				SNew(STextBlock)
				.Text(this, &SChordPBRTab::GetSelectionStatusText)
				.AutoWrapText(true)
			]
			+ SWrapBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
			[
				SNew(SButton)
				.Text(NSLOCTEXT("ChordPBRGenerator", "SetPreviewTargetShort", "Set"))
				.OnClicked(this, &SChordPBRTab::OnSetPreviewTarget)
				.IsEnabled_Lambda([this]() { return GetFirstSelectedActor() != nullptr; })
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.Text(NSLOCTEXT("ChordPBRGenerator", "ClearPreviewTargetShort", "Clear"))
				.OnClicked(this, &SChordPBRTab::OnClearPreviewTarget)
				.IsEnabled_Lambda([this]() { return HasPreviewTarget(); })
			]
		]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("ChordPBRGenerator", "ChatTitle", "Chat / Prompt"))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
				.HintText(NSLOCTEXT("ChordPBRGenerator", "PromptHint", "Describe the material or image you want to preview."))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SWrapBox)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
				.UseAllottedWidth(true)
#endif
				.InnerSlotPadding(FVector2D(8.0f, 2.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "GenerateImages", "Generate Images"))
					.OnClicked(this, &SChordPBRTab::OnGenerateImages)
				]

				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "GeneratePBR", "Generate PBR Maps"))
					.OnClicked(this, &SChordPBRTab::OnGeneratePBRMaps)
				]

				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "Cancel", "Cancel"))
					.OnClicked(this, &SChordPBRTab::OnCancel)
					.IsEnabled_Lambda([this]() { return bIsRunning; })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(this, &SChordPBRTab::GetStatusText)
				.AutoWrapText(true)
			]
		];
}

TSharedRef<SWidget> SChordPBRTab::BuildGallery()
{
	return SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SWrapBox)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
				.UseAllottedWidth(true)
#endif
				.InnerSlotPadding(FVector2D(8.0f, 2.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(this, &SChordPBRTab::GetBackLabel)
					.OnClicked(this, &SChordPBRTab::OnBackToRoot)
					.IsEnabled_Lambda([this]() { return CurrentLayer == EChordGalleryLayer::Detail; })
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(this, &SChordPBRTab::GetPBRImagesLabel)
					.OnClicked(this, &SChordPBRTab::OnEnterDetail)
					.IsEnabled_Lambda([this]() { return CanEnterDetail(); })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(this, &SChordPBRTab::GetGalleryCaption)
					.AutoWrapText(true)
				]

				// Right-aligned buttons: Save and Delete
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(16.0f, 0.0f, 8.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "SaveButton", "Save"))
					.OnClicked(this, &SChordPBRTab::OnSaveAssets)
					.IsEnabled_Lambda([this]() { return Session.IsValid() && Session->HasPBRForImage(CurrentImageIndex); })
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "DeleteButton", "Delete"))
					.OnClicked(this, &SChordPBRTab::OnDeleteCurrentImage)
					.IsEnabled_Lambda([this]() 
					{ 
						return CurrentLayer == EChordGalleryLayer::Root && Session.IsValid() && GetCurrentItemCount() > 0;
					})
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 4.0f)
			[
				SNew(SBorder)
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(NSLOCTEXT("ChordPBRGenerator", "Prev", "<"))
						.OnClicked(this, &SChordPBRTab::OnPreviousImage)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.MinDesiredHeight(420.0f)
						[
							SAssignNew(MainImage, SImage)
							.Image(TAttribute<const FSlateBrush*>::CreateLambda([this]()
							{
								return GetMainBrushForTexture(GetCurrentTexture(), FVector2D(420.0f, 420.0f));
							}))
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(NSLOCTEXT("ChordPBRGenerator", "Next", ">"))
						.OnClicked(this, &SChordPBRTab::OnNextImage)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					if (CurrentLayer == EChordGalleryLayer::Detail)
					{
						if (const FChordPBRMapSet* MapSet = GetCurrentMapSet())
						{
							return FText::Format(
								NSLOCTEXT("ChordPBRGenerator", "PBRMainLabelFmt", "{0} - {1}"),
								FText::FromName(MapSet->Label),
								GetPBRChannelLabel(CurrentPBRChannelIndex));
						}
					}

					if (UTexture2D* Tex = GetCurrentTexture())
					{
						return FText::FromString(GetCurrentImageLabel());
					}
					return NSLOCTEXT("ChordPBRGenerator", "NoImage", "No image selected");
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildThumbnailStrip()
			]
		];
}

TSharedRef<SWidget> SChordPBRTab::BuildThumbnailStrip()
{
	return SNew(SBorder)
		.Padding(4.0f)
		[
			SAssignNew(ThumbnailStrip, SScrollBox)
			.Orientation(Orient_Horizontal)
			.ScrollBarVisibility(EVisibility::Collapsed)
			.AnimateWheelScrolling(false)
		];
}

FText SChordPBRTab::GetSelectionStatusText() const
{
	if (!PreviewTargetActor.IsValid())
	{
		return NSLOCTEXT("ChordPBRGenerator", "PreviewTargetNone", "Preview Target: (none)");
	}

	if (AActor* Actor = PreviewTargetActor.Get())
	{
		return FText::FromString(FString::Printf(TEXT("Preview Target: %s"), *Actor->GetName()));
	}

	return NSLOCTEXT("ChordPBRGenerator", "PreviewTargetInvalid", "Preview Target: (invalid)");
}

FText SChordPBRTab::GetBackLabel() const
{
	return NSLOCTEXT("ChordPBRGenerator", "BackLabel", "< Back");
}

FText SChordPBRTab::GetPBRImagesLabel() const
{
	return NSLOCTEXT("ChordPBRGenerator", "PBRImagesLabel", "PBR Images");
}

FReply SChordPBRTab::OnGenerateImages()
{
	StartGenerateImagesAsync();
	return FReply::Handled();
}

FReply SChordPBRTab::OnGeneratePBRMaps()
{
	StartGeneratePBRAsync();
	return FReply::Handled();
}

FReply SChordPBRTab::OnPreviousImage()
{
	if (CurrentLayer == EChordGalleryLayer::Detail)
	{
		SelectPBRChannel((CurrentPBRChannelIndex - 1 + GetPBRChannelCount()) % GetPBRChannelCount());
	}
	else
	{
		UpdateNavigationIndex(-1);
		OnRootImageSelectionChanged();
		RebuildThumbnails();
	}
	return FReply::Handled();
}

FReply SChordPBRTab::OnNextImage()
{
	if (CurrentLayer == EChordGalleryLayer::Detail)
	{
		SelectPBRChannel((CurrentPBRChannelIndex + 1) % GetPBRChannelCount());
	}
	else
	{
		UpdateNavigationIndex(1);
		OnRootImageSelectionChanged();
		RebuildThumbnails();
	}
	return FReply::Handled();
}

FReply SChordPBRTab::OnBackToRoot()
{
	if (CurrentLayer != EChordGalleryLayer::Root)
	{
		CurrentLayer = EChordGalleryLayer::Root;
		CurrentPBRChannelIndex = 0;
		OnRootImageSelectionChanged();
		RebuildThumbnails();
	}
	return FReply::Handled();
}

FReply SChordPBRTab::OnEnterDetail()
{
	if (CanEnterDetail())
	{
		CurrentLayer = EChordGalleryLayer::Detail;
		CurrentPBRChannelIndex = 0;
		RebuildThumbnails();
	}
	return FReply::Handled();
}

FReply SChordPBRTab::OnSetPreviewTarget()
{
	AActor* NewTarget = GetFirstSelectedActor();
	if (!NewTarget)
	{
		StatusMessage = TEXT("Select an actor to set as preview target.");
		return FReply::Handled();
	}

	RestorePreviewTarget(true);
	PreviewApplier.CaptureOriginalMaterialsForActor(NewTarget);
	PreviewTargetActor = NewTarget;
	StatusMessage = FString::Printf(TEXT("Preview target set: %s"), *NewTarget->GetName());
	ApplyPreviewForCurrentImage(false);
	return FReply::Handled();
}

FReply SChordPBRTab::OnClearPreviewTarget()
{
	ClearPreviewTarget(true);
	StatusMessage = TEXT("Preview target cleared.");
	return FReply::Handled();
}

FReply SChordPBRTab::OnSaveAssets()
{
	if (!Session.IsValid() || !Session->HasPBRForImage(CurrentImageIndex))
	{
		StatusMessage = TEXT("Select an image with PBR maps first.");
		return FReply::Handled();
	}

	const FSaveDialogResult DialogResult = OpenSaveDialog();
	if (DialogResult.bAccepted)
	{
		RunSaveWorkflow(DialogResult);
	}
	return FReply::Handled();
}

FReply SChordPBRTab::OnDeleteCurrentImage()
{
	if (!Session.IsValid() || CurrentLayer != EChordGalleryLayer::Root)
	{
		return FReply::Handled();
	}

	const int32 Count = GetCurrentItemCount();
	if (Count <= 0 || !Session->GetGeneratedImages().IsValidIndex(CurrentImageIndex))
	{
		return FReply::Handled();
	}

	// Confirm deletion
	const FChordGeneratedImageItem* Item = Session->GetMutableImageItem(CurrentImageIndex);
	if (!Item)
	{
		return FReply::Handled();
	}

	FString ConfirmMessage;
	if (Item->bHasPBR)
	{
		ConfirmMessage = FString::Printf(TEXT("Delete '%s' and all associated PBR maps?"), *Item->Label);
	}
	else
	{
		ConfirmMessage = FString::Printf(TEXT("Delete '%s'?"), *Item->Label);
	}

	const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(ConfirmMessage));
	if (Result == EAppReturnType::Yes)
	{
		// Clear preview if this image has one
		if (Item->PreviewMID.IsValid() && PreviewTargetActor.IsValid())
		{
			RestorePreviewTarget(false);
		}

		// Remove the image
		if (Session->RemoveGeneratedImage(CurrentImageIndex))
		{
			StatusMessage = TEXT("Image deleted.");

			// Adjust current index
			const int32 NewCount = Session->GetGeneratedImages().Num();
			if (NewCount > 0)
			{
				CurrentImageIndex = FMath::Clamp(CurrentImageIndex, 0, NewCount - 1);
				OnRootImageSelectionChanged();
			}
			else
			{
				CurrentImageIndex = 0;
			}

			RebuildThumbnails();
		}
		else
		{
			StatusMessage = TEXT("Failed to delete image.");
		}
	}

	return FReply::Handled();
}

void SChordPBRTab::UpdateNavigationIndex(int32 Delta)
{
	const int32 Count = GetCurrentItemCount();
	if (Count <= 0)
	{
		return;
	}

	CurrentImageIndex = (CurrentImageIndex + Delta) % Count;
	if (CurrentImageIndex < 0)
	{
		CurrentImageIndex = Count - 1;
	}
}

int32 SChordPBRTab::GetCurrentItemCount() const
{
	if (!Session.IsValid())
	{
		return 0;
	}

	if (CurrentLayer == EChordGalleryLayer::Root)
	{
		return Session->GetGeneratedImages().Num();
	}

	return GetPBRChannelCount();
}

bool SChordPBRTab::CanEnterDetail() const
{
	if (!Session.IsValid() || CurrentLayer != EChordGalleryLayer::Root)
	{
		return false;
	}

	return Session->HasPBRForImage(CurrentImageIndex);
}

const FSlateBrush* SChordPBRTab::GetBrushForTexture(UTexture2D* Texture, const FVector2D& InDesiredSize) const
{
	if (!Texture)
	{
		return nullptr;
	}

	if (const TSharedPtr<FSlateBrush>* Existing = BrushCache.Find(Texture))
	{
		TSharedPtr<FSlateBrush> Brush = *Existing;
		Brush->SetResourceObject(Texture);
		Brush->ImageSize = InDesiredSize;
		return Brush.Get();
	}

	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = InDesiredSize;
	BrushCache.Add(Texture, Brush);
	return Brush.Get();
}

const FSlateBrush* SChordPBRTab::GetMainBrushForTexture(UTexture2D* Texture, const FVector2D& InDesiredSize) const
{
	if (!Texture || !MainImageBrush.IsValid())
	{
		return nullptr;
	}

	MainImageBrush->SetResourceObject(Texture);
	MainImageBrush->ImageSize = InDesiredSize;
	return MainImageBrush.Get();
}

void SChordPBRTab::RebuildThumbnails()
{
	if (!ThumbnailStrip.IsValid() || !Session.IsValid())
	{
		return;
	}

	const int32 ImageCount = Session->GetGeneratedImages().Num();
	if (ImageCount == 0)
	{
		CurrentImageIndex = 0;
		CurrentLayer = EChordGalleryLayer::Root;
	}
	else
	{
		CurrentImageIndex = FMath::Clamp(CurrentImageIndex, 0, ImageCount - 1);
	}

	bool bForcedRoot = false;
	if (CurrentLayer == EChordGalleryLayer::Detail && !Session->HasPBRForImage(CurrentImageIndex))
	{
		CurrentLayer = EChordGalleryLayer::Root;
		bForcedRoot = true;
	}

	BrushCache.Empty();
	ThumbnailStrip->ClearChildren();
	const FVector2D ThumbSize(96.0f, 96.0f);
	const auto MakeThumbBorder = [](int32 Index, int32 Current) -> FLinearColor
	{
		if (Index == Current)
		{
			return FLinearColor(0.2f, 0.6f, 1.0f, 1.0f);
		}
		return FLinearColor(0, 0, 0, 0);
	};

	if (CurrentLayer == EChordGalleryLayer::Root)
	{
		const TArray<FChordGeneratedImageItem>& Images = Session->GetGeneratedImages();
		for (int32 Index = 0; Index < Images.Num(); ++Index)
		{
			UTexture2D* Texture = Images[Index].Image.Get();
			ThumbnailStrip->AddSlot()
			[
				SNew(SBox)
				.WidthOverride(110.0f)
				.HeightOverride(120.0f)
				[
					SNew(SBorder)
					.Padding(2.0f)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor_Lambda([MakeThumbBorder, Index, this]()
					{
						return MakeThumbBorder(Index, CurrentImageIndex);
					})
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.OnClicked_Lambda([this, Index]() -> FReply
						{
							CurrentImageIndex = Index;
							OnRootImageSelectionChanged();
							RebuildThumbnails();
							return FReply::Handled();
						})
						.Content()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SImage)
								.Image(GetBrushForTexture(Texture, ThumbSize))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 2.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(!Images[Index].Label.IsEmpty() ? Images[Index].Label : (Texture ? Texture->GetName() : FString(TEXT("Image")))))
								.AutoWrapText(true)
								.Justification(ETextJustify::Center)
							]
						]
					]
				]
			];
		}
	}
	else
	{
		const FChordPBRMapSet* MapSet = GetCurrentMapSet();
		if (MapSet)
		{
			const int32 ChannelCount = GetPBRChannelCount();
			for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
			{
				UTexture2D* PreviewTex = GetPBRTextureByChannel(*MapSet, Channel);
				ThumbnailStrip->AddSlot()
				[
					SNew(SBox)
					.WidthOverride(110.0f)
					.HeightOverride(120.0f)
					[
						SNew(SBorder)
						.Padding(2.0f)
						.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
						.BorderBackgroundColor_Lambda([MakeThumbBorder, Channel, this]()
						{
							return MakeThumbBorder(Channel, CurrentPBRChannelIndex);
						})
						[
							SNew(SButton)
							.ButtonStyle(FCoreStyle::Get(), "NoBorder")
							.OnClicked_Lambda([this, Channel]() -> FReply
							{
								SelectPBRChannel(Channel);
								return FReply::Handled();
							})
							.Content()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SImage)
									.Image(GetBrushForTexture(PreviewTex, ThumbSize))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.HAlign(HAlign_Center)
								.Padding(0.0f, 2.0f)
								[
									SNew(STextBlock)
									.Text(GetPBRChannelLabel(Channel))
									.AutoWrapText(true)
									.Justification(ETextJustify::Center)
								]
							]
						]
					]
				];
		}
	}
	}

	if (bForcedRoot)
	{
		OnRootImageSelectionChanged();
	}
}

UTexture2D* SChordPBRTab::GetCurrentTexture() const
{
	if (!Session.IsValid())
	{
		return nullptr;
	}

	const FChordGeneratedImageItem* Item = GetCurrentImageItem();
	if (!Item)
	{
		return nullptr;
	}

	if (CurrentLayer == EChordGalleryLayer::Root)
	{
		return Item->Image.Get();
	}

	if (Item->bHasPBR)
	{
		return GetPBRTextureByChannel(Item->PBRMaps, CurrentPBRChannelIndex);
	}

	return nullptr;
}

const FChordGeneratedImageItem* SChordPBRTab::GetCurrentImageItem() const
{
	if (!Session.IsValid())
	{
		return nullptr;
	}

	const TArray<FChordGeneratedImageItem>& Images = Session->GetGeneratedImages();
	if (Images.IsValidIndex(CurrentImageIndex))
	{
		return &Images[CurrentImageIndex];
	}

	return nullptr;
}

FString SChordPBRTab::GetCurrentImageLabel() const
{
	if (const FChordGeneratedImageItem* Item = GetCurrentImageItem())
	{
		if (!Item->Label.IsEmpty())
		{
			return Item->Label;
		}

		if (Item->Image.IsValid())
		{
			return FPaths::GetBaseFilename(Item->Image->GetName());
		}
	}

	return FString();
}

const FChordPBRMapSet* SChordPBRTab::GetCurrentMapSet() const
{
	if (CurrentLayer != EChordGalleryLayer::Detail)
	{
		return nullptr;
	}

	const FChordGeneratedImageItem* Item = GetCurrentImageItem();
	return Item && Item->bHasPBR ? &Item->PBRMaps : nullptr;
}

FText SChordPBRTab::GetGalleryCaption() const
{
	if (!Session.IsValid() || Session->GetGeneratedImages().Num() == 0)
	{
		return NSLOCTEXT("ChordPBRGenerator", "GalleryEmpty", "No previews yet. Generate images to view them here.");
	}

	const int32 Count = Session->GetGeneratedImages().Num();
	if (CurrentLayer == EChordGalleryLayer::Root)
	{
		return FText::Format(
			NSLOCTEXT("ChordPBRGenerator", "GalleryImagesFmt", "Generated Images ({0}/{1})"),
			FText::AsNumber(CurrentImageIndex + 1),
			FText::AsNumber(Count)
		);
	}

	if (const FChordPBRMapSet* MapSet = GetCurrentMapSet())
	{
		return FText::Format(
			NSLOCTEXT("ChordPBRGenerator", "GalleryPBRFmt", "PBR Maps - {0} ({1})"),
			FText::FromName(MapSet->Label),
			GetPBRChannelLabel(CurrentPBRChannelIndex)
		);
	}

	return NSLOCTEXT("ChordPBRGenerator", "GalleryEmptyPBR", "No PBR previews. Select an image and click Generate PBR Maps.");
}

UTexture2D* SChordPBRTab::GetPBRTextureByChannel(const FChordPBRMapSet& MapSet, int32 ChannelIndex) const
{
	switch (ChannelIndex)
	{
	case 0:
		return MapSet.BaseColor.Get();
	case 1:
		return MapSet.Normal.Get();
	case 2:
		return MapSet.Roughness.Get();
	case 3:
		return MapSet.Metallic.Get();
	case 4:
		return MapSet.Height.Get();
	default:
		break;
	}
	return nullptr;
}

void SChordPBRTab::SelectPBRChannel(int32 ChannelIndex)
{
	CurrentPBRChannelIndex = FMath::Clamp(ChannelIndex, 0, GetPBRChannelCount() - 1);
	RebuildThumbnails();
}

AActor* SChordPBRTab::GetFirstSelectedActor() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	if (USelection* Selection = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				return Actor;
			}
		}
	}

	return nullptr;
}

bool SChordPBRTab::EnsurePreviewMIDForImage(FChordGeneratedImageItem& Item)
{
	if (!Item.bHasPBR)
	{
		return false;
	}

	static const FName BaseColorParam(TEXT("T_BaseColor"));
	static const FName NormalParam(TEXT("T_Normal"));
	static const FName RoughnessParam(TEXT("T_Roughness"));
	static const FName MetallicParam(TEXT("T_Metallic"));
	static const FName HeightParam(TEXT("T_Height"));

	UChordPBRSettings* Settings = GetMutableDefault<UChordPBRSettings>();
	if (!Settings || Settings->PreviewMasterMaterial.IsNull())
	{
		StatusMessage = TEXT("Set Preview Master Material in Project Settings for preview.");
		return false;
	}

	UMaterialInterface* Master = Cast<UMaterialInterface>(Settings->PreviewMasterMaterial.TryLoad());
	if (!Master)
	{
		StatusMessage = TEXT("Preview master material missing or unloadable.");
		return false;
	}

	UMaterialInstanceDynamic* MID = Item.PreviewMID.IsValid() ? Item.PreviewMID.Get() : nullptr;
	if (!MID || MID->Parent != Master)
	{
		MID = UMaterialInstanceDynamic::Create(Master, GetTransientPackage());
		Item.PreviewMID = TStrongObjectPtr<UMaterialInstanceDynamic>(MID);
	}

	if (!MID)
	{
		StatusMessage = TEXT("Failed to build preview material instance.");
		return false;
	}

	MID->SetTextureParameterValue(BaseColorParam, Item.PBRMaps.BaseColor.Get());
	MID->SetTextureParameterValue(NormalParam, Item.PBRMaps.Normal.Get());
	MID->SetTextureParameterValue(RoughnessParam, Item.PBRMaps.Roughness.Get());
	MID->SetTextureParameterValue(MetallicParam, Item.PBRMaps.Metallic.Get());
	MID->SetTextureParameterValue(HeightParam, Item.PBRMaps.Height.Get());
	return true;
}

void SChordPBRTab::ApplyPreviewForCurrentImage(bool bAllowRestoreIfMissing, bool bForceApply)
{
	if (!PreviewTargetActor.IsValid())
	{
		return;
	}

	AActor* Target = PreviewTargetActor.Get();
	if (!Target)
	{
		RestorePreviewTarget(true);
		StatusMessage = TEXT("Preview target is no longer valid.");
		return;
	}

	if (!bForceApply && CurrentLayer != EChordGalleryLayer::Root)
	{
		return;
	}

	if (!Session.IsValid())
	{
		return;
	}

	FChordGeneratedImageItem* MutableItem = Session->GetMutableImageItem(CurrentImageIndex);
	if (!MutableItem)
	{
		if (bAllowRestoreIfMissing)
		{
			PreviewApplier.RestoreOriginalMaterials(false);
		}
		return;
	}

	if (MutableItem->bHasPBR && EnsurePreviewMIDForImage(*MutableItem))
	{
		PreviewApplier.ApplyPreviewMaterialToActor(Target, MutableItem->PreviewMID.Get());
	}
	else if (bAllowRestoreIfMissing)
	{
		PreviewApplier.RestoreOriginalMaterials(false);
	}
}

void SChordPBRTab::OnRootImageSelectionChanged()
{
	if (CurrentLayer == EChordGalleryLayer::Root)
	{
		ApplyPreviewForCurrentImage(true);
	}
}

void SChordPBRTab::RestorePreviewTarget(bool bClearState)
{
	if (PreviewApplier.HasTarget())
	{
		PreviewApplier.RestoreOriginalMaterials(bClearState);
	}

	if (bClearState)
	{
		PreviewTargetActor = nullptr;
	}
}

FText SChordPBRTab::GetPBRChannelLabel(int32 ChannelIndex) const
{
	switch (ChannelIndex)
	{
	case 0:
		return NSLOCTEXT("ChordPBRGenerator", "ChannelBaseColor", "BaseColor");
	case 1:
		return NSLOCTEXT("ChordPBRGenerator", "ChannelNormal", "Normal");
	case 2:
		return NSLOCTEXT("ChordPBRGenerator", "ChannelRoughness", "Roughness");
	case 3:
		return NSLOCTEXT("ChordPBRGenerator", "ChannelMetallic", "Metallic");
	case 4:
		return NSLOCTEXT("ChordPBRGenerator", "ChannelHeight", "Height");
	default:
		break;
	}

	return NSLOCTEXT("ChordPBRGenerator", "ChannelUnknown", "Channel");
}

FText SChordPBRTab::GetStatusText() const
{
	return FText::FromString(StatusMessage);
}

void SChordPBRTab::SetStatusAsync(const FString& InStatus, bool bInRunning)
{
	if (IsInGameThread())
	{
		StatusMessage = InStatus;
		bIsRunning = bInRunning;
		return;
	}

	TWeakPtr<SChordPBRTab> WidgetWeak = SharedThis(this);
	AsyncTask(ENamedThreads::GameThread, [WidgetWeak, InStatus, bInRunning]()
	{
		if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
		{
			Pinned->StatusMessage = InStatus;
			Pinned->bIsRunning = bInRunning;
		}
	});
}

void SChordPBRTab::HandleError(const FString& Message)
{
	UE_LOG(LogChordPBRGenerator, Error, TEXT("%s"), *Message);
	SetStatusAsync(Message, false);
}

void SChordPBRTab::HandleComfyFailure(const FString& Context, const FString& Error)
{
	const FString Composed = FString::Printf(TEXT("%s: %s"), *Context, *Error);
	HandleError(Composed);
}

void SChordPBRTab::EnqueueTask(TFunction<void()> InTask)
{
	Async(EAsyncExecution::ThreadPool, MoveTemp(InTask));
}


void SChordPBRTab::StartGenerateImagesAsync()
{
	if (bIsRunning || !Session.IsValid())
	{
		return;
	}

	UChordPBRSettings* Settings = GetMutableDefault<UChordPBRSettings>();
	const FString Prompt = PromptTextBox.IsValid() ? PromptTextBox->GetText().ToString() : FString();
	const FString BaseLabel = MakeTimestampLabelBase();
	const int32 RequestId = RequestCounter.Increment();
	TWeakPtr<SChordPBRTab> WidgetWeak = SharedThis(this);

	// Use Gemini API if enabled
	if (Settings->Txt2ImgBackend == ETxt2ImgBackend::GeminiAPI)
	{
		if (Settings->GeminiApiKey.IsEmpty())
		{
			HandleError(TEXT("Gemini API key is not configured. Please set it in Project Settings > Plugins > ChordPBRGenerator."));
			return;
		}

		SetStatusAsync(TEXT("Generating image with Gemini API..."), true);

		GeminiClient = MakeShared<FGeminiApiClient>();
		TSharedPtr<FGeminiApiClient> Client = GeminiClient;
		const FString ApiEndpoint = Settings->GeminiApiEndpoint;
		const FString ApiKey = Settings->GeminiApiKey;
		const FString Model = Settings->GeminiModel;

		Client->GenerateImageAsync(ApiEndpoint, ApiKey, Model, Prompt,
			FOnGeminiImageGenerated::CreateLambda([WidgetWeak, RequestId, BaseLabel](UTexture2D* GeneratedTexture, const FString& Error)
			{
				AsyncTask(ENamedThreads::GameThread, [WidgetWeak, RequestId, BaseLabel, GeneratedTexture, Error]()
				{
					if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
					{
						if (Pinned->RequestCounter.GetValue() != RequestId)
						{
							return;
						}

						if (!Error.IsEmpty() || !GeneratedTexture)
						{
							Pinned->HandleError(Error.IsEmpty() ? TEXT("Failed to generate image.") : Error);
							return;
						}

						if (Pinned->Session.IsValid())
						{
							const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *BaseLabel);
							GeneratedTexture->Rename(*UniqueName.ToString());
							Pinned->Session->AddGeneratedImage(GeneratedTexture, BaseLabel);
							Pinned->CurrentLayer = EChordGalleryLayer::Root;
							Pinned->CurrentImageIndex = FMath::Max(0, Pinned->Session->GetGeneratedImages().Num() - 1);
							Pinned->StatusMessage = TEXT("Image generated with Gemini API.");
							Pinned->OnRootImageSelectionChanged();
						}

						Pinned->bIsRunning = false;
						Pinned->RebuildThumbnails();
					}
				});
			}));

		return;
	}

	// Use local ComfyUI workflow
	ComfyClient = MakeShared<FComfyUIClient>(*Settings);
	static uint32 SeedCounter = 0;
	const int32 Seed = static_cast<int32>((FPlatformTime::Cycles64() + SeedCounter++) & static_cast<uint64>(INT32_MAX));

	SetStatusAsync(FString::Printf(TEXT("Submitting image prompt (seed %d)..."), Seed), true);

	TSharedPtr<FComfyUIClient> Client = ComfyClient;

	TSharedPtr<FJsonObject> PromptJson;
	FString Error;
	if (!FComfyWorkflowUtils::PatchTxt2ImgPrompt(*Settings, Prompt, Seed, BaseLabel, PromptJson, Error))
	{
		HandleError(Error);
		return;
	}

	EnqueueTask([WidgetWeak, Client, PromptJson, Settings, RequestId, BaseLabel]()
	{
		FString ErrorLocal;
		auto IsStale = [&WidgetWeak, RequestId]() -> bool
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				return Pinned->RequestCounter.GetValue() != RequestId;
			}
			return true;
		};

		FComfyPromptResponse Response;
		if (!Client->QueuePrompt(PromptJson, Response, ErrorLocal))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(TEXT("Queue prompt failed"), ErrorLocal);
			}
			return;
		}

		if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
		{
			if (Pinned->RequestCounter.GetValue() != RequestId)
			{
				return;
			}
			Pinned->SetStatusAsync(FString::Printf(TEXT("Queued prompt %s. Waiting for output..."), *Response.PromptId), true);
		}

		TSharedPtr<FJsonObject> History;
		auto ProgressCallback = [WidgetWeak, RequestId, PromptId = Response.PromptId](float Progress)
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				if (Pinned->RequestCounter.GetValue() == RequestId)
				{
					Pinned->SetStatusAsync(FString::Printf(TEXT("Generating images... %d%%"), FMath::RoundToInt(Progress * 100.0f)), true);
				}
			}
		};

		if (!Client->WaitForCompletion(Response.PromptId, Response.ClientId, History, ErrorLocal, ProgressCallback))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(FString::Printf(TEXT("Wait for prompt %s"), *Response.PromptId), ErrorLocal);
			}
			return;
		}

		if (IsStale())
		{
			return;
		}

		TArray<FComfyImageReference> Images;
		if (!FComfyWorkflowUtils::ExtractImagesFromHistory(*Settings, History, Images, ErrorLocal))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(TEXT("Parse outputs"), ErrorLocal);
			}
			return;
		}

		if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
		{
			if (Pinned->RequestCounter.GetValue() != RequestId)
			{
				return;
			}
			Pinned->SetStatusAsync(TEXT("Downloading images..."), true);
		}

		struct FDownloadedImage
		{
			FString Name;
			TArray<uint8> Data;
		};

		TArray<FDownloadedImage> Downloaded;
		for (int32 ImageIdx = 0; ImageIdx < Images.Num(); ++ImageIdx)
		{
			const FComfyImageReference& Ref = Images[ImageIdx];
			TArray<uint8> Data;
			if (Client->DownloadImage(Ref, Data, ErrorLocal))
			{
				FDownloadedImage Item;
				Item.Name = (Images.Num() > 1) ? FString::Printf(TEXT("%s_%02d"), *BaseLabel, ImageIdx + 1) : BaseLabel;
				Item.Data = MoveTemp(Data);
				Downloaded.Add(MoveTemp(Item));
			}
		}

		if (Downloaded.Num() == 0)
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				const FString DownloadError = ErrorLocal.IsEmpty() ? TEXT("No images downloaded.") : ErrorLocal;
				Pinned->HandleComfyFailure(TEXT("Download images"), DownloadError);
			}
			return;
		}

		AsyncTask(ENamedThreads::GameThread, [WidgetWeak, Downloaded = MoveTemp(Downloaded), RequestId]() mutable
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				if (Pinned->RequestCounter.GetValue() != RequestId)
				{
					return;
				}

				if (Pinned->Session.IsValid())
				{
					int32 AddedCount = 0;
					for (FDownloadedImage& Item : Downloaded)
					{
						if (UTexture2D* Texture = FChordImageUtils::CreateTextureFromImage(Item.Data, Item.Name))
						{
							const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *Item.Name);
							Texture->Rename(*UniqueName.ToString());
							Pinned->Session->AddGeneratedImage(Texture, Item.Name);
							++AddedCount;
						}
					}

					if (AddedCount > 0)
					{
						Pinned->CurrentLayer = EChordGalleryLayer::Root;
						Pinned->CurrentImageIndex = FMath::Max(0, Pinned->Session->GetGeneratedImages().Num() - 1);
						Pinned->StatusMessage = TEXT("Images downloaded.");
						Pinned->OnRootImageSelectionChanged();
					}
					else
					{
						Pinned->StatusMessage = TEXT("Failed to decode images.");
					}

					Pinned->bIsRunning = false;
					Pinned->RebuildThumbnails();
				}
			}
		});
	});
}

void SChordPBRTab::StartGeneratePBRAsync()
{
	if (bIsRunning || !Session.IsValid())
	{
		HandleError(TEXT("No session or operation already running."));
		return;
	}

	const FChordGeneratedImageItem* CurrentItem = GetCurrentImageItem();
	if (!CurrentItem || !CurrentItem->Image.IsValid())
	{
		HandleError(TEXT("Select a generated image first."));
		return;
	}

	UTexture2D* SourceTexture = CurrentItem->Image.Get();
	if (!SourceTexture)
	{
		HandleError(TEXT("Invalid source texture."));
		return;
	}

	UChordPBRSettings* Settings = GetMutableDefault<UChordPBRSettings>();
	ComfyClient = MakeShared<FComfyUIClient>(*Settings);

	TArray<uint8> PngData;
	FString EncodeError;
	if (!FChordImageUtils::EncodeTextureToPng(SourceTexture, PngData, EncodeError))
	{
		HandleError(EncodeError);
		return;
	}

	const FString SourceLabel = GetCurrentImageLabel();
	const int32 RequestId = RequestCounter.Increment();
	const int32 TargetImageIndex = CurrentImageIndex;
	SetStatusAsync(TEXT("Uploading source image..."), true);

	TWeakPtr<SChordPBRTab> WidgetWeak = SharedThis(this);
	TSharedPtr<FComfyUIClient> Client = ComfyClient;
	const TWeakObjectPtr<UTexture2D> SourceTextureWeak = SourceTexture;

	EnqueueTask([WidgetWeak, Client, Settings, SourceLabel, PngData = MoveTemp(PngData), SourceTextureWeak, RequestId, TargetImageIndex]() mutable
	{
		FString ErrorLocal;
		auto IsStale = [&WidgetWeak, RequestId]() -> bool
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				return Pinned->RequestCounter.GetValue() != RequestId;
			}
			return true;
		};

		FComfyImageReference Uploaded;
		const FString UploadName = FString::Printf(TEXT("%s.png"), *SourceLabel);
		if (!Client->UploadImage(PngData, UploadName, Uploaded, ErrorLocal))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(TEXT("Upload failed"), ErrorLocal);
			}
			return;
		}

		if (IsStale())
		{
			return;
		}

		TSharedPtr<FJsonObject> PromptJson;
		if (!FComfyWorkflowUtils::PatchChordPrompt(*Settings, Uploaded, PromptJson, ErrorLocal))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(TEXT("Template error"), ErrorLocal);
			}
			return;
		}

		FComfyPromptResponse Response;
		if (!Client->QueuePrompt(PromptJson, Response, ErrorLocal))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(TEXT("Queue prompt failed"), ErrorLocal);
			}
			return;
		}

		if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
		{
			if (Pinned->RequestCounter.GetValue() != RequestId)
			{
				return;
			}
			Pinned->SetStatusAsync(FString::Printf(TEXT("Queued PBR prompt %s. Waiting for outputs..."), *Response.PromptId), true);
		}

		TSharedPtr<FJsonObject> History;
		auto ProgressCallback = [WidgetWeak, RequestId, PromptId = Response.PromptId](float Progress)
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				if (Pinned->RequestCounter.GetValue() == RequestId)
				{
					Pinned->SetStatusAsync(FString::Printf(TEXT("Generating PBR maps... %d%%"), FMath::RoundToInt(Progress * 100.0f)), true);
				}
			}
		};

		if (!Client->WaitForCompletion(Response.PromptId, Response.ClientId, History, ErrorLocal, ProgressCallback))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(FString::Printf(TEXT("Wait for PBR outputs %s"), *Response.PromptId), ErrorLocal);
			}
			return;
		}

		if (IsStale())
		{
			return;
		}

		TMap<FString, FComfyImageReference> Channels;
		if (!FComfyWorkflowUtils::ExtractPBRFromHistory(*Settings, History, Channels, ErrorLocal))
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				Pinned->HandleComfyFailure(TEXT("Parse PBR outputs"), ErrorLocal);
			}
			return;
		}

		if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
		{
			if (Pinned->RequestCounter.GetValue() != RequestId)
			{
				return;
			}
			Pinned->SetStatusAsync(TEXT("Downloading PBR maps..."), true);
		}

		struct FDownloadedChannel
		{
			FString ChannelName;
			FString FileName;
			TArray<uint8> Data;
			FString FilePath;
		};

		TArray<FDownloadedChannel> DownloadedChannels;
		const FString CacheRoot = Settings->SavedCacheRoot;
		const FString SafeLabel = FPaths::MakeValidFileName(SourceLabel.IsEmpty() ? Response.PromptId : SourceLabel);
		auto DownloadChannel = [&](const FString& ChannelName) -> bool
		{
			const FComfyImageReference* Ref = Channels.Find(ChannelName);
			if (!Ref)
			{
				ErrorLocal = FString::Printf(TEXT("Missing channel %s."), *ChannelName);
				return false;
			}

			TArray<uint8> Data;
			if (!Client->DownloadImage(*Ref, Data, ErrorLocal))
			{
				return false;
			}

			const FString BaseName = !SourceLabel.IsEmpty() ? SourceLabel : FPaths::GetBaseFilename(Ref->Filename);
			const FString SafeBaseName = FPaths::MakeValidFileName(BaseName);
			FDownloadedChannel Item;
			Item.ChannelName = ChannelName;
			Item.FileName = FString::Printf(TEXT("%s_%s"), *SafeBaseName, *ChannelName);
			Item.Data = MoveTemp(Data);

			const FString CacheDir = FPaths::Combine(CacheRoot, TEXT("PBR"), SafeLabel);
			IFileManager::Get().MakeDirectory(*CacheDir, true);
			const FString TargetName = FString::Printf(TEXT("PBR_%s_%s.png"), *SafeBaseName, *ChannelName);
			const FString TargetPath = FPaths::Combine(CacheDir, TargetName);
			if (FFileHelper::SaveArrayToFile(Item.Data, *TargetPath))
			{
				Item.FilePath = TargetPath;
			}

			DownloadedChannels.Add(MoveTemp(Item));
			return true;
		};

		const FString ChannelsToDownload[] = { TEXT("BaseColor"), TEXT("Normal"), TEXT("Roughness"), TEXT("Metallic"), TEXT("Height") };
		for (const FString& ChannelName : ChannelsToDownload)
		{
			if (IsStale())
			{
				return;
			}

			if (!DownloadChannel(ChannelName))
			{
				if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
				{
					Pinned->HandleComfyFailure(TEXT("Download PBR maps"), ErrorLocal);
				}
				return;
			}
		}

		const FString MapLabel = FString::Printf(TEXT("PBR_%s"), *SafeLabel);

		AsyncTask(ENamedThreads::GameThread, [WidgetWeak, DownloadedChannels = MoveTemp(DownloadedChannels), MapLabel, SourceTextureWeak, RequestId, TargetImageIndex]() mutable
		{
			if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
			{
				if (Pinned->RequestCounter.GetValue() != RequestId)
				{
					return;
				}

					if (Pinned->Session.IsValid())
					{
						FChordPBRMapSet MapSet;
						MapSet.Label = *MapLabel;
						MapSet.SourceImage = SourceTextureWeak;

					for (FDownloadedChannel& Item : DownloadedChannels)
					{
						if (UTexture2D* Tex = FChordImageUtils::CreateTextureFromImage(Item.Data, Item.FileName))
						{
							const FName UniqueTexName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *Item.FileName);
							Tex->Rename(*UniqueTexName.ToString());
							if (Item.ChannelName == TEXT("BaseColor"))
							{
								MapSet.BaseColor = TStrongObjectPtr<UTexture2D>(Tex);
								MapSet.BaseColorPath = Item.FilePath;
								ConfigurePBRTexture(Tex, TEXT("BaseColor"));
							}
							else if (Item.ChannelName == TEXT("Normal"))
							{
								MapSet.Normal = TStrongObjectPtr<UTexture2D>(Tex);
								MapSet.NormalPath = Item.FilePath;
								ConfigurePBRTexture(Tex, TEXT("Normal"));
							}
							else if (Item.ChannelName == TEXT("Roughness"))
							{
								MapSet.Roughness = TStrongObjectPtr<UTexture2D>(Tex);
								MapSet.RoughnessPath = Item.FilePath;
								ConfigurePBRTexture(Tex, TEXT("Roughness"));
							}
							else if (Item.ChannelName == TEXT("Metallic"))
							{
								MapSet.Metallic = TStrongObjectPtr<UTexture2D>(Tex);
								MapSet.MetallicPath = Item.FilePath;
								ConfigurePBRTexture(Tex, TEXT("Metallic"));
							}
							else if (Item.ChannelName == TEXT("Height"))
							{
								MapSet.Height = TStrongObjectPtr<UTexture2D>(Tex);
								MapSet.HeightPath = Item.FilePath;
								ConfigurePBRTexture(Tex, TEXT("Height"));
							}
						}
					}

					if (Pinned->Session->SetPBRMapsForImage(TargetImageIndex, MoveTemp(MapSet)))
					{
						if (FChordGeneratedImageItem* MutableItem = Pinned->Session->GetMutableImageItem(TargetImageIndex))
						{
							Pinned->EnsurePreviewMIDForImage(*MutableItem);
						}

						Pinned->CurrentLayer = EChordGalleryLayer::Detail;
						Pinned->CurrentImageIndex = TargetImageIndex;
						Pinned->CurrentPBRChannelIndex = 0;
						Pinned->StatusMessage = TEXT("PBR maps downloaded.");
						Pinned->ApplyPreviewForCurrentImage(false, true);
					}
					else
					{
						Pinned->StatusMessage = TEXT("Failed to cache PBR maps.");
					}
					Pinned->bIsRunning = false;
					Pinned->RebuildThumbnails();
				}
			}
		});
	});
}

FReply SChordPBRTab::OnCancel()
{
	if (bIsRunning && ComfyClient.IsValid())
	{
		RequestCounter.Increment();
		StatusMessage = TEXT("Cancel requested.");
		bIsRunning = false;

		TWeakPtr<SChordPBRTab> WidgetWeak = SharedThis(this);
		TSharedPtr<FComfyUIClient> Client = ComfyClient;
		EnqueueTask([WidgetWeak, Client]()
		{
			FString Error;
			Client->Cancel(Error);
			if (!Error.IsEmpty())
			{
				AsyncTask(ENamedThreads::GameThread, [WidgetWeak, Error]()
				{
					if (TSharedPtr<SChordPBRTab> Pinned = WidgetWeak.Pin())
					{
						Pinned->StatusMessage = FString::Printf(TEXT("Cancel: %s"), *Error);
					}
				});
			}
		});
	}
	return FReply::Handled();
}

void SChordPBRTab::ClearPreviewTarget(bool bRestoreMaterials)
{
	if (bRestoreMaterials)
	{
		RestorePreviewTarget(true);
	}
	else
	{
		PreviewApplier.Clear();
		PreviewTargetActor = nullptr;
	}
}

SChordPBRTab::FSaveDialogResult SChordPBRTab::OpenSaveDialog()
{
	FSaveDialogResult Result;

	FString BaseName = GetSuggestedBaseName();
	FString SaveRootPath = GetInitialSaveRootPath();
	bool bApplyToTarget = false;
	FString ValidationError;

	TSharedPtr<SEditableTextBox> BaseNameBox;
	TSharedPtr<SEditableTextBox> SavePathBox;
	TSharedPtr<SCheckBox> ApplyCheck;
	TSharedPtr<SButton> OkButton;

	auto RefreshValidation = [&, this]()
	{
		const FString CleanBase = SanitizeUserBaseName(BaseName);
		ValidateSaveInputs(CleanBase, SaveRootPath, ValidationError);
		if (OkButton.IsValid())
		{
			OkButton->SetEnabled(ValidationError.IsEmpty());
		}
	};

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(NSLOCTEXT("ChordPBRGenerator", "SaveDialogTitle", "Save PBR Assets"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	Dialog->SetContent(
		SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("ChordPBRGenerator", "SaveDialogDesc", "Choose asset name and root path. Textures will be saved under Textures/ and the material under Materials/."))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("ChordPBRGenerator", "AssetBaseName", "Asset Base Name"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(BaseNameBox, SEditableTextBox)
					.Text(FText::FromString(BaseName))
					.OnTextChanged_Lambda([&](const FText& NewText)
					{
						BaseName = NewText.ToString();
						RefreshValidation();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("ChordPBRGenerator", "SaveLocation", "Save Root Path"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(SavePathBox, SEditableTextBox)
					.Text(FText::FromString(SaveRootPath))
					.OnTextChanged_Lambda([&](const FText& NewText)
					{
						SaveRootPath = NewText.ToString();
						RefreshValidation();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "BrowseSavePath", "Browse..."))
					.OnClicked_Lambda([&]() -> FReply
					{
						IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
						if (!DesktopPlatform)
						{
							return FReply::Handled();
						}

						FString DefaultDir = FPaths::ProjectContentDir();
						if (SaveRootPath.StartsWith(TEXT("/Game")))
						{
							FString Rel = SaveRootPath;
							Rel.RemoveFromStart(TEXT("/Game"));
							Rel.ReplaceInline(TEXT("/"), TEXT("\\"));
							DefaultDir = FPaths::Combine(FPaths::ProjectContentDir(), Rel);
						}

						FString OutDir;
						if (DesktopPlatform->OpenDirectoryDialog(nullptr, TEXT("Choose Save Folder (Content)"), DefaultDir, OutDir))
						{
							FPaths::NormalizeDirectoryName(OutDir);
							FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
							FPaths::NormalizeDirectoryName(ContentDir);
							if (OutDir.StartsWith(ContentDir))
							{
								FString RelPath = OutDir.RightChop(ContentDir.Len());
								RelPath.ReplaceInline(TEXT("\\"), TEXT("/"));
								SaveRootPath = FString::Printf(TEXT("/Game%s%s"), RelPath.StartsWith(TEXT("/")) ? TEXT("") : TEXT("/"), *RelPath);
								if (SavePathBox.IsValid())
								{
									SavePathBox->SetText(FText::FromString(SaveRootPath));
								}
								RefreshValidation();
							}
						}
						return FReply::Handled();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f)
			[
				SAssignNew(ApplyCheck, SCheckBox)
				.IsEnabled(HasPreviewTarget())
				.IsChecked(bApplyToTarget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([&](ECheckBoxState State)
				{
					bApplyToTarget = (State == ECheckBoxState::Checked);
				})
				.Content()
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("ChordPBRGenerator", "ApplyToTarget", "Apply saved material to Preview Target"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([&]() { return FText::FromString(ValidationError); })
				.ColorAndOpacity(FLinearColor::Red)
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(4.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "CancelSave", "Cancel"))
					.OnClicked_Lambda([&Dialog]()
					{
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SAssignNew(OkButton, SButton)
					.Text(NSLOCTEXT("ChordPBRGenerator", "OkSave", "OK"))
					.IsEnabled(ValidationError.IsEmpty())
					.OnClicked_Lambda([&Dialog, &Result, &BaseName, &SaveRootPath, &bApplyToTarget, this]()
					{
						Result.bAccepted = true;
						Result.BaseName = SanitizeUserBaseName(BaseName);
						Result.SaveRootPath = SaveRootPath;
						Result.bApplyToTarget = bApplyToTarget;
						Dialog->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	);

	RefreshValidation();

	FSlateApplication::Get().AddModalWindow(Dialog, SharedThis(this));
	return Result;
}

bool SChordPBRTab::ValidateSaveInputs(const FString& BaseName, const FString& SaveRootPath, FString& OutError) const
{
	OutError.Reset();
	const FString TrimmedName = SanitizeUserBaseName(BaseName).TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		OutError = TEXT("Asset Base Name is required.");
		return false;
	}

	for (TCHAR Ch : TrimmedName)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
		{
			OutError = TEXT("Base Name must contain only letters, numbers, or underscores.");
			return false;
		}
	}

	const FString TrimmedPath = SaveRootPath.TrimStartAndEnd();
	if (TrimmedPath.IsEmpty())
	{
		OutError = TEXT("Save Root Path is required.");
		return false;
	}

	if (!TrimmedPath.StartsWith(TEXT("/Game")))
	{
		OutError = TEXT("Save Root Path must start with /Game");
		return false;
	}

	FString Dummy = TrimmedPath;
	if (!Dummy.EndsWith(TEXT("/")))
	{
		Dummy += TEXT("/");
	}
	Dummy += TEXT("Dummy/Dummy");
	if (!FPackageName::IsValidLongPackageName(Dummy, true))
	{
		OutError = TEXT("Save Root Path is not a valid package path.");
		return false;
	}

	return true;
}

FString SChordPBRTab::SanitizeAssetName(const FString& InName, int32 MaxLen) const
{
	FString Result;
	Result.Reserve(InName.Len());
	for (TCHAR Ch : InName)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_'))
		{
			Result.AppendChar(Ch);
		}
		else if (FChar::IsWhitespace(Ch))
		{
			Result.AppendChar(TEXT('_'));
		}
	}
	if (Result.Len() > MaxLen)
	{
		Result.LeftInline(MaxLen, false);
	}
	if (Result.IsEmpty())
	{
		Result = TEXT("ChordPBR_Mat");
	}
	return Result;
}

FString SChordPBRTab::SanitizeUserBaseName(const FString& InName, int32 MaxLen) const
{
	const FString Stripped = FPaths::GetBaseFilename(InName);
	return SanitizeAssetName(Stripped, MaxLen);
}

FString SChordPBRTab::MakeTimestampLabelBase() const
{
	const FDateTime Now = FDateTime::Now();
	return FString::Printf(TEXT("IMG_%02d%02d%02d%02d%02d"), Now.GetYear() % 100, Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute());
}

FString SChordPBRTab::FormatGeneratedLabel(const FString& BaseLabel, int32 Index, int32 Total) const
{
	if (Total > 1)
	{
		return FString::Printf(TEXT("%s_%02d"), *BaseLabel, Index + 1);
	}
	return BaseLabel;
}

FString SChordPBRTab::GetSuggestedBaseName() const
{
	if (const FChordGeneratedImageItem* Item = GetCurrentImageItem())
	{
		if (!Item->Label.IsEmpty())
		{
			return SanitizeUserBaseName(Item->Label);
		}

		if (!Item->PBRMaps.Label.IsNone())
		{
			return SanitizeAssetName(Item->PBRMaps.Label.ToString());
		}
	}

	const FString Prompt = PromptTextBox.IsValid() ? PromptTextBox->GetText().ToString() : FString();
	return SanitizeAssetName(Prompt, 32);
}

FString SChordPBRTab::GetInitialSaveRootPath() const
{
	FString StoredPath;
	GConfig->GetString(TEXT("ChordPBRGenerator"), TEXT("LastUsedSaveRootPath"), StoredPath, GEditorPerProjectIni);
	FString DummyError;
	if (ValidateSaveInputs(TEXT("DummyName"), StoredPath, DummyError))
	{
		return StoredPath;
	}

	if (const UChordPBRSettings* Settings = GetDefault<UChordPBRSettings>())
	{
		FString Path = Settings->DefaultSaveRootPath;
		if (ValidateSaveInputs(TEXT("DummyName"), Path, DummyError))
		{
			return Path;
		}
	}

	return TEXT("/Game/ChordPBR");
}

void SChordPBRTab::PersistLastUsedSaveRootPath(const FString& Path) const
{
	GConfig->SetString(TEXT("ChordPBRGenerator"), TEXT("LastUsedSaveRootPath"), *Path, GEditorPerProjectIni);
}

bool SChordPBRTab::RunSaveWorkflow(const FSaveDialogResult& DialogResult)
{
	if (!DialogResult.bAccepted || !Session.IsValid())
	{
		return false;
	}

	const FString CleanBaseName = SanitizeUserBaseName(DialogResult.BaseName);

	FString ValidationError;
	if (!ValidateSaveInputs(CleanBaseName, DialogResult.SaveRootPath, ValidationError))
	{
		HandleError(ValidationError);
		return false;
	}

	FChordGeneratedImageItem* Item = Session->GetMutableImageItem(CurrentImageIndex);
	if (!Item || !Item->bHasPBR)
	{
		StatusMessage = TEXT("No PBR data to save.");
		return false;
	}

	UChordPBRSettings* Settings = GetMutableDefault<UChordPBRSettings>();
	if (!Settings)
	{
		StatusMessage = TEXT("Settings unavailable.");
		return false;
	}

	UMaterialInterface* MasterMaterial = Settings->InstanceMasterMaterial.LoadSynchronous();
	if (!MasterMaterial)
	{
		StatusMessage = TEXT("Instance Master Material is not set. Configure it in Project Settings.");
		FNotificationInfo Info(FText::FromString(StatusMessage));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	const FString CacheRoot = Settings->SavedCacheRoot;
	const FString ExportDir = FPaths::Combine(CacheRoot, TEXT("Exports"), SanitizeAssetName(CleanBaseName));
	IFileManager::Get().MakeDirectory(*ExportDir, true);

	const FString TextureSavePath = FPaths::Combine(DialogResult.SaveRootPath, TEXT("Textures"));
	const FString MaterialSavePath = FPaths::Combine(DialogResult.SaveRootPath, TEXT("Materials"));

	auto EnsurePackageFolder = [](const FString& PackagePath)
	{
		const FString Folder = FPackageName::LongPackageNameToFilename(PackagePath, TEXT(""));
		IFileManager::Get().MakeDirectory(*Folder, true);
	};
	EnsurePackageFolder(TextureSavePath);
	EnsurePackageFolder(MaterialSavePath);

	struct FChannelToSave
	{
		FString Channel;
		UTexture2D* Texture = nullptr;
		FString FilePath;
		bool bRequired = true;
		UTexture2D* ImportedTexture = nullptr;
	};

	TArray<FChannelToSave> Channels;
	Channels.Add({ TEXT("BaseColor"), Item->PBRMaps.BaseColor.Get(), Item->PBRMaps.BaseColorPath, true });
	Channels.Add({ TEXT("Normal"), Item->PBRMaps.Normal.Get(), Item->PBRMaps.NormalPath, true });
	Channels.Add({ TEXT("Roughness"), Item->PBRMaps.Roughness.Get(), Item->PBRMaps.RoughnessPath, true });
	Channels.Add({ TEXT("Metallic"), Item->PBRMaps.Metallic.Get(), Item->PBRMaps.MetallicPath, true });
	Channels.Add({ TEXT("Height"), Item->PBRMaps.Height.Get(), Item->PBRMaps.HeightPath, false });
	const bool bHadHeight = Channels.Last().Texture != nullptr || !Channels.Last().FilePath.IsEmpty();

	auto EnsureChannelFile = [&](FChannelToSave& Channel) -> bool
	{
		if (!Channel.Texture)
		{
			return !Channel.bRequired;
		}

		if (!Channel.FilePath.IsEmpty() && FPaths::FileExists(Channel.FilePath))
		{
			return true;
		}

		const FString TargetName = FString::Printf(TEXT("%s_%s.png"), *CleanBaseName, *Channel.Channel);
		const FString TargetPath = FPaths::Combine(ExportDir, TargetName);
		FString Error;
		if (FChordImageUtils::SaveTextureToPng(Channel.Texture, TargetPath, Error))
		{
			Channel.FilePath = TargetPath;
			return true;
		}

		HandleError(FString::Printf(TEXT("Export %s failed: %s"), *Channel.Channel, *Error));
		return false;
	};

	for (FChannelToSave& Channel : Channels)
	{
		if (!EnsureChannelFile(Channel))
		{
			if (Channel.bRequired)
			{
				return false;
			}
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UObject*> ImportedObjects;
	TArray<TPair<UPackage*, UObject*>> PackagesToSave;

	for (FChannelToSave& Channel : Channels)
	{
		if (Channel.FilePath.IsEmpty())
		{
			continue;
		}

		const FString CandidatePath = FPaths::Combine(TextureSavePath, FString::Printf(TEXT("%s_%s"), *CleanBaseName, *Channel.Channel));
		FString UniquePackageName;
		FString UniqueAssetName;
		AssetTools.CreateUniqueAssetName(CandidatePath, TEXT(""), UniquePackageName, UniqueAssetName);

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = Channel.FilePath;
		Task->DestinationPath = TextureSavePath;
		Task->DestinationName = UniqueAssetName;
		Task->bAutomated = true;
		Task->bSave = false;

		AssetTools.ImportAssetTasks({ Task });

		if (Task->ImportedObjectPaths.Num() > 0)
		{
			UObject* ImportedObj = LoadObject<UObject>(nullptr, *Task->ImportedObjectPaths[0]);
			if (UTexture2D* ImportedTex = Cast<UTexture2D>(ImportedObj))
			{
				Channel.ImportedTexture = ImportedTex;
				ImportedObjects.Add(ImportedTex);

				if (Channel.Channel == TEXT("BaseColor"))
				{
					ImportedTex->SRGB = true;
				}
				else
				{
					ImportedTex->SRGB = false;
					if (Channel.Channel == TEXT("Normal"))
					{
						ImportedTex->CompressionSettings = TC_Normalmap;
					}
					else if (Channel.Channel == TEXT("Height"))
					{
						ImportedTex->CompressionSettings = TC_Grayscale;
					}
					else
					{
						ImportedTex->CompressionSettings = TC_Masks;
					}
				}

				ImportedTex->PostEditChange();
				ImportedTex->MarkPackageDirty();
				PackagesToSave.AddUnique(TPair<UPackage*, UObject*>(ImportedTex->GetOutermost(), ImportedTex));
			}
		}
	}

	FString MICPackageName;
	FString MICAssetName;
	AssetTools.CreateUniqueAssetName(FPaths::Combine(MaterialSavePath, CleanBaseName), TEXT(""), MICPackageName, MICAssetName);
	UPackage* MICPackage = CreatePackage(*MICPackageName);
	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(MICPackage, *MICAssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!MIC)
	{
		HandleError(TEXT("Failed to create material instance package."));
		return false;
	}

	MIC->SetParentEditorOnly(MasterMaterial);

	auto GetImported = [&](const FString& Channel) -> UTexture2D*
	{
		for (const FChannelToSave& Entry : Channels)
		{
			if (Entry.Channel == Channel)
			{
				return Entry.ImportedTexture;
			}
		}
		return nullptr;
	};

	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("T_BaseColor")), GetImported(TEXT("BaseColor")));
	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("T_Normal")), GetImported(TEXT("Normal")));
	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("T_Roughness")), GetImported(TEXT("Roughness")));
	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("T_Metallic")), GetImported(TEXT("Metallic")));
	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("T_Height")), GetImported(TEXT("Height")));

	MIC->PostEditChange();
	MICPackage->MarkPackageDirty();
	PackagesToSave.AddUnique(TPair<UPackage*, UObject*>(MICPackage, MIC));

	auto SavePackageHelper = [](UPackage* Package, UObject* Asset) -> bool
	{
		if (!Package)
		{
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	};

	for (const TPair<UPackage*, UObject*>& Pair : PackagesToSave)
	{
		if (Pair.Value)
		{
			SavePackageHelper(Pair.Key, Pair.Value);
		}
	}

	PersistLastUsedSaveRootPath(DialogResult.SaveRootPath);

	const bool bShouldApplyToTarget = DialogResult.bApplyToTarget && PreviewTargetActor.IsValid();

	if (bShouldApplyToTarget)
	{
		if (AActor* Target = PreviewTargetActor.Get())
		{
			PreviewApplier.ApplyPreviewMaterialToActor(Target, MIC);
		}
		ClearPreviewTarget(false);
	}

	const int32 SavedCount = ImportedObjects.Num();
	FString Summary = FString::Printf(TEXT("Saved %d textures and MIC: %s"), SavedCount, *MICPackageName);
	if (!bHadHeight)
	{
		Summary += TEXT(" (Height missing)");
	}
	if (bShouldApplyToTarget)
	{
		Summary += TEXT(" | Applied to preview target");
	}
	StatusMessage = Summary;
	FNotificationInfo Info(FText::FromString(Summary));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	return true;
}


