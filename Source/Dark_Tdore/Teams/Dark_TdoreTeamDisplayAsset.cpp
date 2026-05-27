// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamDisplayAsset.h"
#include "Components/MeshComponent.h"
#include "NiagaraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamDisplayAsset)

// ============ ApplyToMaterial — 应用到动态材质实例 ============

void UDark_TdoreTeamDisplayAsset::ApplyToMaterial(UMaterialInstanceDynamic* Material)
{
	if (Material)
	{
		for (const auto& KVP : ScalarParameters)  { Material->SetScalarParameterValue(KVP.Key, KVP.Value); }
		for (const auto& KVP : ColorParameters)   { Material->SetVectorParameterValue(KVP.Key, FVector(KVP.Value)); }
		for (const auto& KVP : TextureParameters) { Material->SetTextureParameterValue(KVP.Key, KVP.Value); }
	}
}

// ============ ApplyToMeshComponent — 应用到网格组件所有材质槽 ============

void UDark_TdoreTeamDisplayAsset::ApplyToMeshComponent(UMeshComponent* MeshComponent)
{
	if (!MeshComponent) return;

	for (const auto& KVP : ScalarParameters) { MeshComponent->SetScalarParameterValueOnMaterials(KVP.Key, KVP.Value); }
	for (const auto& KVP : ColorParameters)  { MeshComponent->SetVectorParameterValueOnMaterials(KVP.Key, FVector(KVP.Value)); }

	const TArray<UMaterialInterface*> MaterialInterfaces = MeshComponent->GetMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialInterfaces.Num(); ++MaterialIndex)
	{
		if (UMaterialInterface* MaterialInterface = MaterialInterfaces[MaterialIndex])
		{
			UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(MaterialInterface);
			if (!DynamicMaterial) { DynamicMaterial = MeshComponent->CreateAndSetMaterialInstanceDynamic(MaterialIndex); }
			for (const auto& KVP : TextureParameters) { DynamicMaterial->SetTextureParameterValue(KVP.Key, KVP.Value); }
		}
	}
}

// ============ ApplyToNiagaraComponent — 应用到 Niagara 粒子组件 ============

void UDark_TdoreTeamDisplayAsset::ApplyToNiagaraComponent(UNiagaraComponent* NiagaraComponent)
{
	if (!NiagaraComponent) return;
	for (const auto& KVP : ScalarParameters)  { NiagaraComponent->SetVariableFloat(KVP.Key, KVP.Value); }
	for (const auto& KVP : ColorParameters)   { NiagaraComponent->SetVariableLinearColor(KVP.Key, KVP.Value); }
	for (const auto& KVP : TextureParameters) { NiagaraComponent->SetVariableTexture(KVP.Key, KVP.Value); }
}

// ============ ApplyToActor — 一键应用到 Actor 的所有组件 ============

void UDark_TdoreTeamDisplayAsset::ApplyToActor(AActor* TargetActor, bool bIncludeChildActors)
{
	if (TargetActor)
	{
		TargetActor->ForEachComponent(bIncludeChildActors, [this](UActorComponent* InComponent)
		{
			if (UMeshComponent* MeshComp = Cast<UMeshComponent>(InComponent)) { ApplyToMeshComponent(MeshComp); }
			else if (UNiagaraComponent* NiagaraComp = Cast<UNiagaraComponent>(InComponent)) { ApplyToNiagaraComponent(NiagaraComp); }
		});
	}
}
