// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/Dark_TdoreWeaponActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/Dark_TdoreEquipmentInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreWeaponActor)

ADark_TdoreWeaponActor::ADark_TdoreWeaponActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	WeaponRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponRoot"));
	SetRootComponent(WeaponRootComponent);

	WeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMeshComponent->SetupAttachment(WeaponRootComponent);
	WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMeshComponent->SetGenerateOverlapEvents(false);
}

void ADark_TdoreWeaponActor::BeginPlay()
{
	Super::BeginPlay();

	if (bDisableCollisionOnBeginPlay)
	{
		SetWeaponCollisionEnabled(false);
	}
}

void ADark_TdoreWeaponActor::InitializeFromEquipmentInstance(UDark_TdoreEquipmentInstance* InEquipmentInstance)
{
	OwningEquipmentInstance = InEquipmentInstance;
	K2_OnInitializedFromEquipmentInstance(InEquipmentInstance);
}

APawn* ADark_TdoreWeaponActor::GetOwningPawn() const
{
	return OwningEquipmentInstance ? OwningEquipmentInstance->GetPawn() : nullptr;
}

void ADark_TdoreWeaponActor::SetWeaponCollisionEnabled(bool bEnabled)
{
	if (!WeaponMeshComponent)
	{
		return;
	}

	WeaponMeshComponent->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	WeaponMeshComponent->SetGenerateOverlapEvents(bEnabled);
}

void ADark_TdoreWeaponActor::SetWeaponVisible(bool bVisible)
{
	SetActorHiddenInGame(!bVisible);
	if (WeaponMeshComponent)
	{
		WeaponMeshComponent->SetVisibility(bVisible, true);
	}
}
