// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAbilityCost_Attribute.h"
#include "Dark_TdoreGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Dark_TdoreLogChannels.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAbilityCost_Attribute)

UE_DEFINE_GAMEPLAY_TAG(TAG_ABILITY_FAIL_COST, "Ability.ActivateFail.Cost");

UDark_TdoreAbilityCost_Attribute::UDark_TdoreAbilityCost_Attribute()
{
	Amount.SetValue(1.0f);
	FailureTag = TAG_ABILITY_FAIL_COST;
}

bool UDark_TdoreAbilityCost_Attribute::CheckCost(const UDark_TdoreGameplayAbility* Ability,
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	const float CurrentValue = ASC->GetNumericAttribute(Attribute);
	const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);
	const float CostValue = Amount.GetValueAtLevel(AbilityLevel);

	UE_LOG(LogDark_TdoreGAS, Display, TEXT("[Cost:Check] %s | %s = %.1f | 消耗 = %.1f | %s"),
		*GetNameSafe(Ability),
		*Attribute.GetName(),
		CurrentValue,
		CostValue,
		CurrentValue >= CostValue ? TEXT("通过 ✅") : TEXT("不足 ❌"));

	if (CurrentValue < CostValue)
	{
		if (OptionalRelevantTags && FailureTag.IsValid())
		{
			OptionalRelevantTags->AddTag(FailureTag);
		}
		return false;
	}

	return true;
}

void UDark_TdoreAbilityCost_Attribute::ApplyCost(const UDark_TdoreGameplayAbility* Ability,
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (!ActorInfo->IsNetAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC)
	{
		return;
	}

	const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);
	const float CostValue = Amount.GetValueAtLevel(AbilityLevel);
	const float BeforeValue = ASC->GetNumericAttribute(Attribute);

	ASC->ApplyModToAttribute(Attribute, EGameplayModOp::Additive, -CostValue);

	const float AfterValue = ASC->GetNumericAttribute(Attribute);

	UE_LOG(LogDark_TdoreGAS, Display, TEXT("[Cost:Apply] %s | %s: %.1f → %.1f (消耗: %.1f)"),
		*GetNameSafe(Ability),
		*Attribute.GetName(),
		BeforeValue,
		AfterValue,
		CostValue);
}
