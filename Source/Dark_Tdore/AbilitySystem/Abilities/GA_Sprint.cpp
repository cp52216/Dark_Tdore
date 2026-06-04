// Copyright Epic Games, Inc. All Rights Reserved.

#include "GA_Sprint.h"

#include "Character/Dark_TdoreCharacterMovementComponent.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GA_Sprint)

UGA_Sprint::UGA_Sprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::WhileInputActive;
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Independent;
}

void UGA_Sprint::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SetSprintPressed(ActorInfo, true);
}

void UGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	SetSprintPressed(ActorInfo, false);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Sprint::SetSprintPressed(const FGameplayAbilityActorInfo* ActorInfo, bool bSprintPressed) const
{
	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	UDark_TdoreCharacterMovementComponent* MoveComp = Character ? Cast<UDark_TdoreCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;

	if (MoveComp)
	{
		MoveComp->SetSprintPressed(bSprintPressed);
	}
}
