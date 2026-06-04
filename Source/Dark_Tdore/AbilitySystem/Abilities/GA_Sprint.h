// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dark_TdoreGameplayAbility.h"
#include "GA_Sprint.generated.h"

/**
 * Hold-input sprint ability. Activation raises movement speed; release/cancel restores it.
 */
UCLASS()
class UGA_Sprint : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Sprint(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void SetSprintPressed(const FGameplayAbilityActorInfo* ActorInfo, bool bSprintPressed) const;
};
