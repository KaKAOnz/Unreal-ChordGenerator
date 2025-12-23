// Copyright 2025 KaKAOnz. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

class FChordTextureGenerator
{
public:
	static UTexture2D* CreateCheckerTexture(int32 Size, const FLinearColor& ColorA, const FLinearColor& ColorB, int32 SquaresPerSide);
	static UTexture2D* CreateLinearGradientTexture(int32 Size, const FLinearColor& StartColor, const FLinearColor& EndColor, bool bHorizontal);
	static UTexture2D* CreateRadialFadeTexture(int32 Size, const FLinearColor& CenterColor, const FLinearColor& EdgeColor);

private:
	static UTexture2D* CreateTextureInternal(int32 Size, const TFunctionRef<FColor(int32 X, int32 Y)>& PixelGenerator);
};
