// Copyright ChordPBRGenerator

#pragma once

#include "IDetailCustomization.h"

class FChordPBRSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
