// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/Dark_TdoreWeaponActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dark_Tdore.h"
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

	UE_LOG(LogDark_Tdore, Log, TEXT("[WeaponActor] 构造: Actor=%s Root=%s Mesh=%s MeshCollision=%s Authority=%s"),
		*GetNameSafe(this),
		*GetNameSafe(WeaponRootComponent),
		*GetNameSafe(WeaponMeshComponent),
		WeaponMeshComponent ? TEXT("NoCollision") : TEXT("null"),
		HasAuthority() ? TEXT("true") : TEXT("false"));
}

void ADark_TdoreWeaponActor::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogDark_Tdore, Log, TEXT("[WeaponActor] BeginPlay: Actor=%s bDisableCollisionOnBeginPlay=%s Authority=%s"),
		*GetNameSafe(this),
		bDisableCollisionOnBeginPlay ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"));

	if (bDisableCollisionOnBeginPlay)
	{
		SetWeaponCollisionEnabled(false);
	}
}

void ADark_TdoreWeaponActor::InitializeFromEquipmentInstance(UDark_TdoreEquipmentInstance* InEquipmentInstance)
{
	OwningEquipmentInstance = InEquipmentInstance;

	const APawn* Pawn = InEquipmentInstance ? InEquipmentInstance->GetPawn() : nullptr;
	UE_LOG(LogDark_Tdore, Log, TEXT("[WeaponActor] 装备实例初始化: Actor=%s EquipmentInstance=%s Pawn=%s Authority=%s"),
		*GetNameSafe(this),
		*GetNameSafe(InEquipmentInstance),
		*GetNameSafe(Pawn),
		HasAuthority() ? TEXT("true") : TEXT("false"));

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
		UE_LOG(LogDark_Tdore, Warning, TEXT("[WeaponActor] 碰撞切换失败: Actor=%s Mesh=null Enabled=%s"),
			*GetNameSafe(this),
			bEnabled ? TEXT("true") : TEXT("false"));
		return;
	}

	const ECollisionEnabled::Type NewCollision = bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision;

	UE_LOG(LogDark_Tdore, Log, TEXT("[WeaponActor] 碰撞切换: Actor=%s Mesh=%s Enabled=%s Collision=%s Authority=%s"),
		*GetNameSafe(this),
		*GetNameSafe(WeaponMeshComponent),
		bEnabled ? TEXT("true") : TEXT("false"),
		bEnabled ? TEXT("QueryOnly") : TEXT("NoCollision"),
		HasAuthority() ? TEXT("true") : TEXT("false"));

	WeaponMeshComponent->SetCollisionEnabled(NewCollision);
	WeaponMeshComponent->SetGenerateOverlapEvents(bEnabled);
}

void ADark_TdoreWeaponActor::SetWeaponVisible(bool bVisible)
{
	UE_LOG(LogDark_Tdore, Log, TEXT("[WeaponActor] 可见性切换: Actor=%s Visible=%s PreviousHidden=%s Authority=%s"),
		*GetNameSafe(this),
		bVisible ? TEXT("true") : TEXT("false"),
		IsHidden() ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"));

	SetActorHiddenInGame(!bVisible);
	if (WeaponMeshComponent)
	{
		WeaponMeshComponent->SetVisibility(bVisible, true);
	}
}
