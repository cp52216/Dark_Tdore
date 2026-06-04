// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAnimInstance.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimEnums.h"
#include "Character/Dark_TdoreCharacterMovementComponent.h"
#include "Dark_Tdore.h"
#include "Dark_TdoreCharacter.h"
#include "Equipment/Dark_TdoreEquipmentManagerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "Weapons/Dark_TdoreWeaponInstance.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAnimInstance)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Anim_Status_Weapon_Equipped, "Status.Weapon.Equipped");

UDark_TdoreAnimInstance::UDark_TdoreAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
}

void UDark_TdoreAnimInstance::InitializeWithAbilitySystem(UAbilitySystemComponent* ASC)
{
	check(ASC);

	NativeAbilitySystemComponent = ASC;
	GameplayTagPropertyMap.Initialize(this, ASC);
}

void UDark_TdoreAnimInstance::RefreshWeaponState()
{
	if (!NativeAbilitySystemComponent)
	{
		if (AActor* OwningActor = GetOwningActor())
		{
			NativeAbilitySystemComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor);
		}
	}

	const bool bPreviousHasWeapon = bHasWeapon;
	const bool bHasWeaponFromTag = NativeAbilitySystemComponent
		&& NativeAbilitySystemComponent.Get()->HasMatchingGameplayTag(TAG_Anim_Status_Weapon_Equipped);

	bool bHasWeaponFromEquipment = false;
	if (AActor* OwningActor = GetOwningActor())
	{
		if (UDark_TdoreEquipmentManagerComponent* EquipmentManager = OwningActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>())
		{
			bHasWeaponFromEquipment = EquipmentManager->GetFirstInstanceOfType(UDark_TdoreWeaponInstance::StaticClass()) != nullptr;
		}
	}

	bHasWeapon = bHasWeaponFromTag || bHasWeaponFromEquipment;

	if (bPreviousHasWeapon != bHasWeapon)
	{
		UE_LOG(LogDark_Tdore, Log, TEXT("AnimInstance weapon state changed: AnimInstance=%s Owner=%s HasWeapon=%s ASC=%s FromTag=%s FromEquipment=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwningActor()),
			bHasWeapon ? TEXT("true") : TEXT("false"),
			*GetNameSafe(NativeAbilitySystemComponent.Get()),
			bHasWeaponFromTag ? TEXT("true") : TEXT("false"),
			bHasWeaponFromEquipment ? TEXT("true") : TEXT("false"));
	}
}

#if WITH_EDITOR
EDataValidationResult UDark_TdoreAnimInstance::IsDataValid(FDataValidationContext& Context) const
{
	Super::IsDataValid(Context);

	GameplayTagPropertyMap.IsDataValid(this, Context);

	return ((Context.GetNumErrors() > 0) ? EDataValidationResult::Invalid : EDataValidationResult::Valid);
}
#endif

void UDark_TdoreAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;

	NativeCharacter = Cast<ACharacter>(GetOwningActor());
	NativeMovementComponent = NativeCharacter ? NativeCharacter->GetCharacterMovement() : nullptr;

	if (AActor* OwningActor = GetOwningActor())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor))
		{
			InitializeWithAbilitySystem(ASC);
		}
	}

	RefreshWeaponState();
}

void UDark_TdoreAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!NativeCharacter)
	{
		NativeCharacter = Cast<ACharacter>(GetOwningActor());
	}

	NativeMovementComponent = NativeCharacter ? NativeCharacter->GetCharacterMovement() : nullptr;
	if (!NativeCharacter || !NativeMovementComponent)
	{
		NativeVelocity = FVector::ZeroVector;
		NativeGroundSpeed = 0.0f;
		NativeDirection = 0.0f;
		bNativeShouldMove = false;
		bNativeIsFalling = false;
		bHasWeapon = false;
		GroundDistance = -1.0f;
		return;
	}

	RefreshWeaponState();

	NativeVelocity = NativeMovementComponent->Velocity;
	NativeGroundSpeed = NativeVelocity.Size2D();

	const FVector LocalVelocity = NativeCharacter->GetActorRotation().UnrotateVector(NativeVelocity);
	const float RawDirection = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
	NativeDirection = NativeMovementComponent->bOrientRotationToMovement
		? FMath::Clamp(RawDirection, -45.0f, 45.0f)
		: RawDirection;

	bNativeShouldMove = NativeGroundSpeed > NativeShouldMoveThreshold
		&& !NativeMovementComponent->GetCurrentAcceleration().IsNearlyZero();
	bNativeIsFalling = NativeMovementComponent->IsFalling();

	const ADark_TdoreCharacter* Character = Cast<ADark_TdoreCharacter>(GetOwningActor());
	if (!Character)
	{
		return;
	}

	UDark_TdoreCharacterMovementComponent* CharMoveComp = CastChecked<UDark_TdoreCharacterMovementComponent>(Character->GetCharacterMovement());
	const FDark_TdoreCharacterGroundInfo& GroundInfo = CharMoveComp->GetGroundInfo();
	GroundDistance = GroundInfo.GroundDistance;
}
