// Copyright ChordPBRGenerator

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UMeshComponent;
class UMaterialInterface;

struct FPreviewComponentSnapshot
{
	TWeakObjectPtr<UMeshComponent> Component;
	TArray<TWeakObjectPtr<UMaterialInterface>> SlotMaterials;
};

class FPreviewMaterialApplier
{
public:
	void CaptureOriginalMaterialsForActor(AActor* Actor);
	void ApplyPreviewMaterialToActor(AActor* Actor, UMaterialInterface* PreviewMaterial);
	void RestoreOriginalMaterials(bool bClearState = true);
	void Clear();

	AActor* GetTargetActor() const { return TargetActor.Get(); }
	bool HasTarget() const { return TargetActor.IsValid(); }

private:
	TWeakObjectPtr<AActor> TargetActor;
	TArray<FPreviewComponentSnapshot> CapturedComponents;
};
