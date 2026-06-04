// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/Dark_TdoreWeaponInstance.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/Dark_TdoreAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dark_Tdore.h"
#include "GameFramework/Character.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreWeaponInstance)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Status_Weapon_Equipped, "Status.Weapon.Equipped");

UDark_TdoreWeaponInstance::UDark_TdoreWeaponInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDark_TdoreWeaponInstance::OnEquipped()
{
	TimeLastEquipped = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	SetWeaponEquippedTag(true);
	const bool bLinkedAnimLayer = LinkAnimLayer(true);
	if (!bLinkedAnimLayer)
	{
		SetWeaponEquippedTag(false);
	}

	Super::OnEquipped();

	UE_LOG(LogDark_Tdore, Log, TEXT("Weapon equipped: Instance=%s Pawn=%s LinkedAnimLayer=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetPawn()),
		bLinkedAnimLayer ? TEXT("true") : TEXT("false"));

	PlayWeaponMontage(EquipMontage);
}

void UDark_TdoreWeaponInstance::OnUnequipped()
{
	SetWeaponEquippedTag(false);
	LinkAnimLayer(false);
	PlayWeaponMontage(UnequipMontage);
	Super::OnUnequipped();
}

void UDark_TdoreWeaponInstance::UpdateFiringTime()
{
	TimeLastFired = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

float UDark_TdoreWeaponInstance::GetTimeSinceLastInteractedWith() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	const double LastInteractionTime = FMath::Max(TimeLastEquipped, TimeLastFired);
	return static_cast<float>(World->GetTimeSeconds() - LastInteractionTime);
}

TSubclassOf<UAnimInstance> UDark_TdoreWeaponInstance::PickAnimLayer(bool bEquipped) const
{
	return PickBestAnimLayer(bEquipped, FGameplayTagContainer());
}

TSubclassOf<UAnimInstance> UDark_TdoreWeaponInstance::PickBestAnimLayer(bool bEquipped, const FGameplayTagContainer& CosmeticTags) const
{
	const FDark_TdoreAnimLayerSelectionSet& SetToQuery = bEquipped ? EquippedAnimSet : UnequippedAnimSet;
	return SetToQuery.SelectBestLayer(CosmeticTags);
}

bool UDark_TdoreWeaponInstance::LinkAnimLayer(bool bEquipped)
{
	ACharacter* Character = Cast<ACharacter>(GetPawn());
	USkeletalMeshComponent* MeshComponent = Character ? Character->GetMesh() : nullptr;
	if (!MeshComponent)
	{
		UE_LOG(LogDark_Tdore, Warning, TEXT("Weapon anim layer link failed: Instance=%s Pawn=%s Character=%s Mesh=null"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			*GetNameSafe(Character));
		return false;
	}

	if (LinkedAnimLayer)
	{
		MeshComponent->UnlinkAnimClassLayers(LinkedAnimLayer);
		LinkedAnimLayer = nullptr;
	}

	if (bEquipped)
	{
		const TSubclassOf<UAnimInstance> AnimLayer = PickAnimLayer(true);
		if (!AnimLayer)
		{
			UE_LOG(LogDark_Tdore, Warning, TEXT("Weapon anim layer link failed: Instance=%s Pawn=%s Mesh=%s AnimInstance=%s PickAnimLayer=null"),
				*GetNameSafe(this),
				*GetNameSafe(GetPawn()),
				*GetNameSafe(MeshComponent),
				*GetNameSafe(MeshComponent->GetAnimInstance()));
			return false;
		}

		MeshComponent->LinkAnimClassLayers(AnimLayer);
		LinkedAnimLayer = AnimLayer;

		UE_LOG(LogDark_Tdore, Log, TEXT("Weapon anim layer linked: Instance=%s Pawn=%s Mesh=%s BaseAnimInstance=%s LayerClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			*GetNameSafe(MeshComponent),
			*GetNameSafe(MeshComponent->GetAnimInstance()),
			*GetNameSafe(AnimLayer.Get()));
		return true;
	}

	UE_LOG(LogDark_Tdore, Log, TEXT("Weapon anim layer unlinked: Instance=%s Pawn=%s Mesh=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetPawn()),
		*GetNameSafe(MeshComponent));
	return true;
}

void UDark_TdoreWeaponInstance::SetWeaponEquippedTag(bool bEquipped) const
{
	APawn* OwningPawn = GetPawn();
	UAbilitySystemComponent* ASC = OwningPawn ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningPawn) : nullptr;
	if (ASC)
	{
		ASC->SetLooseGameplayTagCount(TAG_Status_Weapon_Equipped, bEquipped ? 1 : 0);

		UE_LOG(LogDark_Tdore, Log, TEXT("Weapon equipped tag updated: Pawn=%s ASC=%s Equipped=%s HasTag=%s Authority=%s"),
			*GetNameSafe(OwningPawn),
			*GetNameSafe(ASC),
			bEquipped ? TEXT("true") : TEXT("false"),
			ASC->HasMatchingGameplayTag(TAG_Status_Weapon_Equipped) ? TEXT("true") : TEXT("false"),
			OwningPawn->HasAuthority() ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogDark_Tdore, Warning, TEXT("Weapon equipped tag update failed: Pawn=%s ASC=null Equipped=%s"),
			*GetNameSafe(OwningPawn),
			bEquipped ? TEXT("true") : TEXT("false"));
	}

	if (ACharacter* Character = Cast<ACharacter>(OwningPawn))
	{
		if (USkeletalMeshComponent* MeshComponent = Character->GetMesh())
		{
			if (UDark_TdoreAnimInstance* AnimInstance = Cast<UDark_TdoreAnimInstance>(MeshComponent->GetAnimInstance()))
			{
				AnimInstance->RefreshWeaponState();
			}
		}
	}
}

void UDark_TdoreWeaponInstance::PlayWeaponMontage(UAnimMontage* MontageToPlay) const
{
	ACharacter* Character = Cast<ACharacter>(GetPawn());
	if (Character && MontageToPlay)
	{
		Character->PlayAnimMontage(MontageToPlay);
	}
}
