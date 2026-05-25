// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGameplayAbility.h"
#include "Abilities/Dark_TdoreAbilityCost.h"
#include "Dark_TdoreLogChannels.h"

// ============ 构造 ============

UDark_TdoreGameplayAbility::UDark_TdoreGameplayAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 默认激活策略：按下按键时激活
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;
	// 默认互斥模式：独立运行
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Independent;
}

// ============ 辅助方法 ============

UDark_TdoreAbilitySystemComponent* UDark_TdoreGameplayAbility::GetDarkTdoreAbilitySystemComponentFromActorInfo() const
{
	return Cast<UDark_TdoreAbilitySystemComponent>(CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr);
}

void UDark_TdoreGameplayAbility::TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const
{
	// 参考 Lyra：检查激活策略和技能状态，通过 ASC 激活而非 this
	if (ActorInfo && !Spec.IsActive() && (ActivationPolicy == EDark_TdoreAbilityActivationPolicy::OnSpawn))
	{
		UDark_TdoreAbilitySystemComponent* ASC = Cast<UDark_TdoreAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());
		if (ASC)
		{
			ASC->TryActivateAbility(Spec.Handle);
		}
	}
}

// ============ UGameplayAbility 接口覆写 ============

bool UDark_TdoreGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	// 基本检查：ASC 必须有效
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return false;
	}

	// 参考 Lyra：检查激活组是否被阻塞
	// 如果此技能属于 Exclusive_Blocking 组且该组中已有活跃技能，则不允许激活
	if (UDark_TdoreAbilitySystemComponent* DarkASC = Cast<UDark_TdoreAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
	{
		if (DarkASC->IsActivationGroupBlocked(ActivationGroup))
		{
			return false;
		}
	}

	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

// 参考 Lyra ULyraGameplayAbility::CheckCost
// 先调用父类（检查引擎内置 CostGE/CooldownGE），再遍历 AdditionalCosts
bool UDark_TdoreGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags) || !ActorInfo)
	{
		return false;
	}

	for (const TObjectPtr<UDark_TdoreAbilityCost>& Cost : AdditionalCosts)
	{
		if (Cost && !Cost->CheckCost(this, Handle, ActorInfo, OptionalRelevantTags))
		{
			return false;
		}
	}

	return true;
}

// 参考 Lyra ULyraGameplayAbility::ApplyCost
// 先调用父类，然后遍历 AdditionalCosts 逐个扣除
void UDark_TdoreGameplayAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
	check(ActorInfo);

	for (const TObjectPtr<UDark_TdoreAbilityCost>& Cost : AdditionalCosts)
	{
		if (Cost)
		{
			// 命中才扣除的消耗（如弹药）
			if (Cost->ShouldOnlyApplyCostOnHit())
			{
				// TODO: 需要 DetermineIfAbilityHitTarget — 接入 Targeting 后实现
				continue;
			}
			Cost->ApplyCost(this, Handle, ActorInfo, ActivationInfo);
		}
	}
}

void UDark_TdoreGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("技能授予: %s"), *GetNameSafe(this));

	// 通知蓝图：技能已被添加
	K2_OnAbilityAdded();

	// OnSpawn 技能在授予后立即激活（前提是 AvatarActor 已就绪）
	if (ActivationPolicy == EDark_TdoreAbilityActivationPolicy::OnSpawn && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		TryActivateAbilityOnSpawn(ActorInfo, Spec);
	}
}

void UDark_TdoreGameplayAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	UE_LOG(LogDark_TdoreGAS, Log, TEXT("技能移除: %s"), *GetNameSafe(this));

	// 通知蓝图：技能已被移除
	K2_OnAbilityRemoved();

	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UDark_TdoreGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("技能激活: %s"), *GetNameSafe(this));
}

void UDark_TdoreGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UE_LOG(LogDark_TdoreGAS, Verbose, TEXT("技能结束: %s（取消: %d）"), *GetNameSafe(this), bWasCancelled);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UDark_TdoreGameplayAbility::OnPawnAvatarSet()
{
	UE_LOG(LogDark_TdoreGAS, Log, TEXT("Pawn化身已设置: %s"), *GetNameSafe(this));

	// 通知蓝图：Pawn 化身已就绪
	K2_OnPawnAvatarSet();
}
