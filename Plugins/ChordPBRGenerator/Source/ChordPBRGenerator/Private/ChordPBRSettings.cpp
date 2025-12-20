// Copyright ChordPBRGenerator

#include "ChordPBRSettings.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

UChordPBRSettings::UChordPBRSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ChordPBRGenerator");

	// Default to ComfyUI
	Txt2ImgBackend = ETxt2ImgBackend::ComfyUI;

	// ComfyUI Settings
	ComfyHttpBaseUrl = TEXT("http://127.0.0.1:8188");
	RequestTimeoutSeconds = 300.0f;
	bUseWebSocketProgress = true;
	PollingFallbackIntervalSeconds = 0.5f;

	SavedCacheRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ChordPBRGenerator")));

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ChordPBRGenerator")))
	{
		Txt2ImgApiPromptPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/chord_sdxl_t2i_text_to_image.json"));
		ChordImg2PbrApiPromptPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/chord_image_to_material.json"));
	}
	else
	{
		Txt2ImgApiPromptPath = TEXT("Resources/chord_sdxl_t2i_text_to_image.json");
		ChordImg2PbrApiPromptPath = TEXT("Resources/chord_image_to_material.json");
	}

	PreviewMasterMaterial = FSoftObjectPath(TEXT("/ChordPBRGenerator/Preview/M_PreviewPBR.M_PreviewPBR"));
	DefaultSaveRootPath = TEXT("/Game/ChordPBR");
	InstanceMasterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/ChordPBRGenerator/Preview/M_PreviewPBR.M_PreviewPBR")));

	// Text-to-Image Bindings (for ComfyUI)
	Txt2ImgBinding.PromptNodeIdentifier = TEXT("2");
	Txt2ImgBinding.PromptInputName = TEXT("text");
	Txt2ImgBinding.SeedNodeIdentifier = TEXT("4");
	Txt2ImgBinding.SeedInputName = TEXT("seed");

	// PBR Generation Bindings
	ChordBinding.LoadImageNodeId = 2;
	ChordBinding.LoadImageInputName = TEXT("image");

	ChordBinding.BaseColor.NodeId = 4;
	ChordBinding.BaseColor.FilenameHintContains = TEXT("basecolor");

	ChordBinding.Normal.NodeId = 5;
	ChordBinding.Normal.FilenameHintContains = TEXT("normal");

	ChordBinding.Roughness.NodeId = 6;
	ChordBinding.Roughness.FilenameHintContains = TEXT("roughness");

	ChordBinding.Metallic.NodeId = 7;
	ChordBinding.Metallic.FilenameHintContains = TEXT("metalness");

	ChordBinding.Height.NodeId = 9;
	ChordBinding.Height.FilenameHintContains = TEXT("height");
}
