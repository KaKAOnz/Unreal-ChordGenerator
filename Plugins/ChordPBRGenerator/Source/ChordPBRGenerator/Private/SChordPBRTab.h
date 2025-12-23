// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChordPBRSession.h"
#include "ChordPBRSettings.h"
#include "PreviewMaterialApplier.h"
#include "HAL/ThreadSafeCounter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateBrush.h"

class FComfyUIClient;
class FGeminiApiClient;
class AActor;

class SChordPBRTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChordPBRTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChordPBRTab();

	// SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// UI callbacks
	FText GetSelectionStatusText() const;
	FText GetBackLabel() const;
	FText GetPBRImagesLabel() const;
	FReply OnGenerateImages();
	FReply OnGeneratePBRMaps();
	FReply OnPreviousImage();
	FReply OnNextImage();
	FReply OnCancel();
	FReply OnBackToRoot();
	FReply OnEnterDetail();
	FReply OnSetPreviewTarget();
	FReply OnClearPreviewTarget();
	FReply OnSaveAssets();
	FReply OnDeleteCurrentImage();

	// Helpers
	void UpdateNavigationIndex(int32 Delta);
	int32 GetCurrentItemCount() const;
	bool CanEnterDetail() const;
	const FSlateBrush* GetBrushForTexture(class UTexture2D* Texture, const FVector2D& InDesiredSize) const;
	const FSlateBrush* GetMainBrushForTexture(class UTexture2D* Texture, const FVector2D& InDesiredSize) const;
	TSharedRef<SWidget> BuildGallery();
	TSharedRef<SWidget> BuildChat();
	TSharedRef<SWidget> BuildThumbnailStrip();
	void RebuildThumbnails();
	UTexture2D* GetCurrentTexture() const;
	const FChordGeneratedImageItem* GetCurrentImageItem() const;
	const FChordPBRMapSet* GetCurrentMapSet() const;
	FText GetGalleryCaption() const;
	UTexture2D* GetPBRTextureByChannel(const FChordPBRMapSet& MapSet, int32 ChannelIndex) const;
	int32 GetPBRChannelCount() const { return 5; }
	void SelectPBRChannel(int32 ChannelIndex);
	FText GetPBRChannelLabel(int32 ChannelIndex) const;
	FText GetStatusText() const;
	void SetStatusAsync(const FString& InStatus, bool bInRunning);
	void AppendSystemMessage(const FString& Message);
	void EnqueueTask(TFunction<void()> InTask);
	void HandleError(const FString& Message);
	void HandleComfyFailure(const FString& Context, const FString& Error);
	void StartGenerateImagesAsync();
	void StartGeneratePBRAsync();
	AActor* GetFirstSelectedActor() const;
	bool EnsurePreviewMIDForImage(FChordGeneratedImageItem& Item);
	void ApplyPreviewForCurrentImage(bool bAllowRestoreIfMissing = true, bool bForceApply = false);
	void OnRootImageSelectionChanged();
	void RestorePreviewTarget(bool bClearState);
	bool HasPreviewTarget() const { return PreviewTargetActor.IsValid(); }
	void ClearPreviewTarget(bool bRestoreMaterials);

	struct FSaveDialogResult
	{
		bool bAccepted = false;
		FString BaseName;
		FString SaveRootPath;
		bool bApplyToTarget = false;
	};

	FSaveDialogResult OpenSaveDialog();
	bool ValidateSaveInputs(const FString& BaseName, const FString& SaveRootPath, FString& OutError) const;
	FString GetSuggestedBaseName() const;
	FString GetInitialSaveRootPath() const;
	void PersistLastUsedSaveRootPath(const FString& Path) const;
	bool RunSaveWorkflow(const FSaveDialogResult& DialogResult);
	FString SanitizeAssetName(const FString& InName, int32 MaxLen = 48) const;
	FString SanitizeUserBaseName(const FString& InName, int32 MaxLen = 48) const;
	FString GetCurrentImageLabel() const;
	FString MakeTimestampLabelBase() const;
	FString FormatGeneratedLabel(const FString& BaseLabel, int32 Index, int32 Total) const;

private:
	TSharedPtr<class SMultiLineEditableTextBox, ESPMode::ThreadSafe> PromptTextBox;
	TSharedPtr<class SImage, ESPMode::ThreadSafe> MainImage;
	TSharedPtr<class SScrollBox, ESPMode::ThreadSafe> ThumbnailStrip;

	TSharedPtr<FSlateBrush> MainImageBrush;
	mutable TMap<UTexture2D*, TSharedPtr<FSlateBrush>> BrushCache;

	TSharedPtr<FChordPBRSession> Session;

	EChordGalleryLayer CurrentLayer = EChordGalleryLayer::Root;
	int32 CurrentImageIndex = 0;
	int32 CurrentPBRChannelIndex = 0;
	TWeakObjectPtr<AActor> PreviewTargetActor;
	FPreviewMaterialApplier PreviewApplier;

	bool bIsRunning = false;
	FString StatusMessage;
	TSharedPtr<FComfyUIClient> ComfyClient;
	TSharedPtr<FGeminiApiClient> GeminiClient;
	FThreadSafeCounter RequestCounter;
};
