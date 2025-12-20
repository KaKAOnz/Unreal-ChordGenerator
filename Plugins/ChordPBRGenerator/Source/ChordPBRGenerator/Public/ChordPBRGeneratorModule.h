// Copyright ChordPBRGenerator

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FUICommandList;

class FChordPBRGeneratorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnChordTab(const class FSpawnTabArgs& Args);

private:
	TSharedPtr<FUICommandList> PluginCommands;
};

DECLARE_LOG_CATEGORY_EXTERN(LogChordPBRGenerator, Log, All);
