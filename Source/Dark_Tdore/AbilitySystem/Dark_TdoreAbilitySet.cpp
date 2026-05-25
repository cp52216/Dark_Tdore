// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAbilitySet.h"
#include "Dark_TdoreAbilitySystemComponent.h"
#include "Dark_TdoreLogChannels.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"

UDark_TdoreAbilitySet::UDark_TdoreAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDark_TdoreAbilitySet::GiveToAbilitySystem(UDark_TdoreAbilitySystemComponent* ASC, UObject* SourceObject) const
{
	if (!ASC)
	{
		return;
	}

	// ====== 授予技能 ======
	for (const FDark_TdoreAbilitySet_Ability& AbilityEntry : GrantedAbilities)
	{
		if (!AbilityEntry.Ability) continue;

		// 创建 Skill Spec（参考 Lyra LyraAbilitySet.cpp:116-118）
		FGameplayAbilitySpec AbilitySpec(AbilityEntry.Ability, AbilityEntry.AbilityLevel);
		AbilitySpec.SourceObject = SourceObject;
		AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityEntry.InputTag);

		ASC->GiveAbility(AbilitySpec);

		UE_LOG(LogDark_TdoreGAS, Log, TEXT("AbilitySet 授予技能: %s (InputTag: %s)"),
			*AbilityEntry.Ability->GetName(),
			*AbilityEntry.InputTag.ToString());
	}

	// ====== 授予 GameplayEffect ======
	for (const FDark_TdoreAbilitySet_Effect& EffectEntry : GrantedEffects)
	{
		if (!EffectEntry.GameplayEffect) continue;

		UE_LOG(LogDark_TdoreGAS, Log, TEXT("AbilitySet 授予 GE: %s (Level: %.1f) → ASC: %s"),
			*EffectEntry.GameplayEffect->GetName(), EffectEntry.EffectLevel, *GetNameSafe(ASC));

		FGameplayEffectContextHandle EffectContext = ASC->MakeEffectContext();
		EffectContext.AddSourceObject(SourceObject);

		FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectEntry.GameplayEffect, EffectEntry.EffectLevel, EffectContext);
		if (SpecHandle.IsValid())
		{
			const FActiveGameplayEffectHandle ActiveHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			UE_LOG(LogDark_TdoreGAS, Log, TEXT("  → GE 已应用: %s"), ActiveHandle.IsValid() ? TEXT("成功") : TEXT("失败"));
		}
		else
		{
			UE_LOG(LogDark_TdoreGAS, Error, TEXT("  → MakeOutgoingSpec 失败!"));
		}
	}

	// 激活 OnSpawn 技能
	ASC->TryActivateAbilitiesOnSpawn();

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("AbilitySet 授予完成: %d 技能, %d 效果"),
		GrantedAbilities.Num(), GrantedEffects.Num());
}
