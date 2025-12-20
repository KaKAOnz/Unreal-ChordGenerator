// Copyright ChordPBRGenerator

#include "ChordPBRGeneratorModule.h"

#include "ChordPBRSettings.h"
#include "ChordPBRSettingsCustomization.h"
#include "LevelEditor.h"
#include "Framework/Docking/TabManager.h"
#include "PropertyEditorModule.h"
#include "SChordPBRTab.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"

IMPLEMENT_MODULE(FChordPBRGeneratorModule, ChordPBRGenerator)

DEFINE_LOG_CATEGORY(LogChordPBRGenerator);

static const FName ChordPBRTabName(TEXT("ChordPBRTab"));

void FChordPBRGeneratorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ChordPBRTabName,
		FOnSpawnTab::CreateRaw(this, &FChordPBRGeneratorModule::SpawnChordTab)
	)
		.SetDisplayName(NSLOCTEXT("ChordPBRGenerator", "TabTitle", "Chord PBR"))
		.SetTooltipText(NSLOCTEXT("ChordPBRGenerator", "TooltipText", "Open the Chord PBR generator tab"))
		.SetIcon(FSlateIcon())
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomClassLayout(
		UChordPBRSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChordPBRSettingsCustomization::MakeInstance)
	);
	PropertyModule.NotifyCustomizationModuleChanged();

	RegisterMenus();
}

void FChordPBRGeneratorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyModule.UnregisterCustomClassLayout(UChordPBRSettings::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ChordPBRTabName);
}

void FChordPBRGeneratorModule::RegisterMenus()
{
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::LoadModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		// Add to Window menu for easy discovery
		TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
		Extender->AddMenuExtension(
			"WindowLayout",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& Builder)
			{
				Builder.AddMenuEntry(
					NSLOCTEXT("ChordPBRGenerator", "OpenTab", "Chord PBR"),
					NSLOCTEXT("ChordPBRGenerator", "OpenTabTooltip", "Open the Chord PBR tab"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(ChordPBRTabName);
					}))
				);
			})
		);

		LevelEditorModule->GetMenuExtensibilityManager()->AddExtender(Extender);
	}
}

TSharedRef<SDockTab> FChordPBRGeneratorModule::SpawnChordTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(NSLOCTEXT("ChordPBRGenerator", "TabTitle", "Chord PBR"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SChordPBRTab)
		];
}
