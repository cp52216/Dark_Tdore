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

void FDark_TdoreAbilitySet_GrantedHandles::AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle)
{
	if (Handle.IsValid())
	{
		AbilitySpecHandles.Add(Handle);
	}
}

void FDark_TdoreAbilitySet_GrantedHandles::AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle)
{
	if (Handle.IsValid())
	{
		GameplayEffectHandles.Add(Handle);
	}
}

void FDark_TdoreAbilitySet_GrantedHandles::TakeFromAbilitySystem(UDark_TdoreAbilitySystemComponent* ASC)
{
	// 装备卸下时调用。只回收由这件装备授予的内容，不影响 PawnData 默认授予的技能。
	if (!ASC)
	{
		return;
	}

	// 先清理技能，避免卸下装备后输入仍能激活武器技能。
	for (const FGameplayAbilitySpecHandle& Handle : AbilitySpecHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}

	// 再移除装备附带的持续 GE，例如武器属性加成、标签状态等。
	for (const FActiveGameplayEffectHandle& Handle : GameplayEffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}

	AbilitySpecHandles.Reset();
	GameplayEffectHandles.Reset();
}

void UDark_TdoreAbilitySet::GiveToAbilitySystem(UDark_TdoreAbilitySystemComponent* ASC, FDark_TdoreAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject) const
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

		const FGameplayAbilitySpecHandle AbilitySpecHandle = ASC->GiveAbility(AbilitySpec);
		if (OutGrantedHandles)
		{
			// 装备系统会传入 OutGrantedHandles，用于卸下时 ClearAbility。
			OutGrantedHandles->AddAbilitySpecHandle(AbilitySpecHandle);
		}

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
			if (OutGrantedHandles)
			{
				// 装备系统会传入 OutGrantedHandles，用于卸下时 RemoveActiveGameplayEffect。
				OutGrantedHandles->AddGameplayEffectHandle(ActiveHandle);
			}
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
