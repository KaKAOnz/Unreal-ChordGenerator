// Copyright ChordPBRGenerator

#include "PreviewMaterialApplier.h"

#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

void FPreviewMaterialApplier::CaptureOriginalMaterialsForActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	RestoreOriginalMaterials();

	TargetActor = Actor;
	CapturedComponents.Reset();

	TArray<UMeshComponent*> MeshComponents;
	Actor->GetComponents<UMeshComponent>(MeshComponents);
	for (UMeshComponent* MeshComp : MeshComponents)
	{
		if (!MeshComp)
		{
			continue;
		}

		FPreviewComponentSnapshot Snapshot;
		Snapshot.Component = MeshComp;

		const int32 SlotCount = MeshComp->GetNumMaterials();
		Snapshot.SlotMaterials.Reserve(SlotCount);
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			Snapshot.SlotMaterials.Add(MeshComp->GetMaterial(SlotIndex));
		}

		CapturedComponents.Add(MoveTemp(Snapshot));
	}
}

void FPreviewMaterialApplier::ApplyPreviewMaterialToActor(AActor* Actor, UMaterialInterface* PreviewMaterial)
{
	if (!Actor || !PreviewMaterial)
	{
		return;
	}

	TArray<UMeshComponent*> MeshComponents;
	Actor->GetComponents<UMeshComponent>(MeshComponents);
	for (UMeshComponent* MeshComp : MeshComponents)
	{
		if (!MeshComp)
		{
			continue;
		}

		const int32 SlotCount = MeshComp->GetNumMaterials();
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			MeshComp->SetMaterial(SlotIndex, PreviewMaterial);
		}

		MeshComp->MarkRenderStateDirty();
	}
}

void FPreviewMaterialApplier::RestoreOriginalMaterials(bool bClearState)
{
	for (FPreviewComponentSnapshot& Snapshot : CapturedComponents)
	{
		UMeshComponent* MeshComp = Snapshot.Component.Get();
		if (!MeshComp)
		{
			continue;
		}

		const int32 SlotCount = Snapshot.SlotMaterials.Num();
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			if (UMaterialInterface* Material = Snapshot.SlotMaterials[SlotIndex].Get())
			{
				MeshComp->SetMaterial(SlotIndex, Material);
			}
			else
			{
				MeshComp->SetMaterial(SlotIndex, nullptr);
			}
		}

		MeshComp->MarkRenderStateDirty();
	}

	if (bClearState)
	{
		Clear();
	}
}

void FPreviewMaterialApplier::Clear()
{
	CapturedComponents.Reset();
	TargetActor = nullptr;
}
