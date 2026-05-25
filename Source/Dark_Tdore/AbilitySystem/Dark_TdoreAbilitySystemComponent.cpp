// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAbilitySystemComponent.h"
#include "Dark_TdoreAbilityTagRelationshipMapping.h"
#include "Dark_TdoreGameplayAbility.h"
#include "Dark_TdoreGameplayEffectContext.h"
#include "Dark_TdoreLogChannels.h"
#include "GameplayTagContainer.h"

// ============ 构造 & 生命周期 ============

UDark_TdoreAbilitySystemComponent::UDark_TdoreAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 将所有激活组计数器初始化为 0
	for (int32& Count : ActivationGroupCounts)
	{
		Count = 0;
	}
}

void UDark_TdoreAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// ============ EffectContext 工厂 ============

FGameplayEffectContextHandle UDark_TdoreAbilitySystemComponent::MakeEffectContext() const
{
	// 返回扩展的 EffectContext，使 ExecutionCalculation 可以访问 CartridgeID 等扩展数据
	FDark_TdoreGameplayEffectContext* Context = new FDark_TdoreGameplayEffectContext();
	return FGameplayEffectContextHandle(Context);
}

// ============ 输入路由（参考 Lyra 模式） ============

void UDark_TdoreAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag)))
			{
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.AddUnique(AbilitySpec.Handle);
			}
		}
	}
}

void UDark_TdoreAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag)))
			{
				InputReleasedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.Remove(AbilitySpec.Handle);
			}
		}
	}
}

void UDark_TdoreAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	// ===== 处理本帧按下的输入：激活技能 =====
	{
		TArray<FGameplayAbilitySpecHandle> AbilitiesToActivate;
		for (const FGameplayAbilitySpecHandle& SpecHandle : InputPressedSpecHandles)
		{
			if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
			{
				// 只激活当前未运行的技能
				if (Spec->Ability && !Spec->IsActive())
				{
					// 如果技能是非 Independent 组（互斥组），先取消同组其他技能
					const UDark_TdoreGameplayAbility* AbilityCDO = Cast<UDark_TdoreGameplayAbility>(Spec->Ability);
					if (AbilityCDO && AbilityCDO->GetActivationGroup() != EDark_TdoreAbilityActivationGroup::Independent)
					{
						CancelActivationGroupAbilities(AbilityCDO->GetActivationGroup(), nullptr, false);
					}
					AbilitiesToActivate.AddUnique(SpecHandle);
				}
			}
		}

		// 批量激活所有符合条件的技能
		for (const FGameplayAbilitySpecHandle& AbilityHandle : AbilitiesToActivate)
		{
			TryActivateAbility(AbilityHandle);
		}

		// 清空按下队列
		InputPressedSpecHandles.Reset();
	}

	// ===== 处理本帧释放的输入：取消 WhileInputActive 技能 =====
	{
		for (const FGameplayAbilitySpecHandle& SpecHandle : InputReleasedSpecHandles)
		{
			if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
			{
				if (Spec->Ability && Spec->IsActive())
				{
					// 只有 WhileInputActive 类型的技能在释放时取消
					// OnInputTriggered 类型的技能不受释放影响
					const UDark_TdoreGameplayAbility* AbilityCDO = Cast<UDark_TdoreGameplayAbility>(Spec->Ability);
					if (AbilityCDO && AbilityCDO->GetActivationPolicy() == EDark_TdoreAbilityActivationPolicy::WhileInputActive)
					{
						CancelAbilityHandle(SpecHandle);
					}
				}
			}
		}

		// 清空释放队列
		InputReleasedSpecHandles.Reset();
	}
}

void UDark_TdoreAbilitySystemComponent::ClearAbilityInput()
{
	// 清空所有输入缓冲（角色死亡、状态切换等场景）
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

// ============ 激活组管理（互斥控制） ============

bool UDark_TdoreAbilitySystemComponent::IsActivationGroupBlocked(EDark_TdoreAbilityActivationGroup Group) const
{
	// 只有 Exclusive_Blocking 组会阻塞其他技能激活
	// 当该组中有至少一个活跃技能时，不允许同组新技能激活
	if (Group == EDark_TdoreAbilityActivationGroup::Exclusive_Blocking)
	{
		return ActivationGroupCounts[static_cast<uint8>(EDark_TdoreAbilityActivationGroup::Exclusive_Blocking)] > 0;
	}
	return false;
}

void UDark_TdoreAbilitySystemComponent::AddAbilityToActivationGroup(EDark_TdoreAbilityActivationGroup Group, UDark_TdoreGameplayAbility* Ability)
{
	check(Ability);
	check(Group < EDark_TdoreAbilityActivationGroup::MAX);

	ActivationGroupCounts[static_cast<uint8>(Group)]++;
}

void UDark_TdoreAbilitySystemComponent::RemoveAbilityFromActivationGroup(EDark_TdoreAbilityActivationGroup Group, UDark_TdoreGameplayAbility* Ability)
{
	check(Ability);
	check(Group < EDark_TdoreAbilityActivationGroup::MAX);

	ActivationGroupCounts[static_cast<uint8>(Group)]--;
}

void UDark_TdoreAbilitySystemComponent::CancelActivationGroupAbilities(EDark_TdoreAbilityActivationGroup Group, UDark_TdoreGameplayAbility* IgnoreAbility, bool bReplicateCancelAbility)
{
	// 遍历所有技能，取消同组中活跃的其他技能
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (AbilitySpec.Ability && AbilitySpec.IsActive())
		{
			UDark_TdoreGameplayAbility* Ability = Cast<UDark_TdoreGameplayAbility>(AbilitySpec.Ability);
			if (Ability && Ability->GetActivationGroup() == Group && Ability != IgnoreAbility)
			{
				// 取消技能，可通过参数控制是否复制到客户端
				Ability->CancelAbility(AbilitySpec.Handle, AbilityActorInfo.Get(), Ability->GetCurrentActivationInfo(), bReplicateCancelAbility);
			}
		}
	}
}

// ============ OnSpawn 技能 ============

void UDark_TdoreAbilitySystemComponent::TryActivateAbilitiesOnSpawn()
{
	// 遍历所有已授予技能，激活激活策略为 OnSpawn 且未运行的技能
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (AbilitySpec.Ability)
		{
			const UDark_TdoreGameplayAbility* AbilityCDO = Cast<UDark_TdoreGameplayAbility>(AbilitySpec.Ability);
			if (AbilityCDO && AbilityCDO->GetActivationPolicy() == EDark_TdoreAbilityActivationPolicy::OnSpawn && !AbilitySpec.IsActive())
			{
				TryActivateAbility(AbilitySpec.Handle);
			}
		}
	}
}

// ============ 动态标签效果 ============

void UDark_TdoreAbilitySystemComponent::AddDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	// 简化实现：通过 GameplayEffect 动态添加 GameplayTag
	// 完整实现需要配置一个专门用于添加标签的 GE 类
	UE_LOG(LogDark_TdoreGAS, Log, TEXT("AddDynamicTagGameplayEffect: %s（需要配置 GE 类）"), *Tag.ToString());
}

void UDark_TdoreAbilitySystemComponent::RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	UE_LOG(LogDark_TdoreGAS, Log, TEXT("RemoveDynamicTagGameplayEffect: %s"), *Tag.ToString());
}

// ============ 引擎回调覆写 ============

void UDark_TdoreAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputPressed(Spec);
}

void UDark_TdoreAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);
}

void UDark_TdoreAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	Super::NotifyAbilityActivated(Handle, Ability);

	// 技能激活时，将其加入对应激活组（用于互斥管理）
	if (UDark_TdoreGameplayAbility* DarkAbility = Cast<UDark_TdoreGameplayAbility>(Ability))
	{
		AddAbilityToActivationGroup(DarkAbility->GetActivationGroup(), DarkAbility);
	}

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("技能已激活: %s"), *GetNameSafe(Ability));
}

void UDark_TdoreAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);

	// 技能结束时，将其移出激活组
	if (UDark_TdoreGameplayAbility* DarkAbility = Cast<UDark_TdoreGameplayAbility>(Ability))
	{
		RemoveAbilityFromActivationGroup(DarkAbility->GetActivationGroup(), DarkAbility);
	}

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("技能已结束: %s（取消: %d）"), *GetNameSafe(Ability), bWasCancelled);
}

// ============ 技能标签关系映射（TagRelationshipMapping）============
// 参考 Lyra ULyraAbilitySystemComponent

// 覆写 ApplyAbilityBlockAndCancelTags：在引擎默认的 Block/Cancel 逻辑之前，
// 通过 TagRelationshipMapping 扩展需要 Block 和 Cancel 的标签。
// 例如：轻攻击激活时，Mapping 中配置了 "Action.Attack.Light → Cancel: Action.Move.Dash"
//       则所有拥有 Action.Move.Dash 标签的闪避技能将被自动取消。
void UDark_TdoreAbilitySystemComponent::ApplyAbilityBlockAndCancelTags(
	const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility,
	bool bEnableBlockTags, const FGameplayTagContainer& BlockTags,
	bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags)
{
	// 复制原始标签，后续通过 Mapping 扩展
	FGameplayTagContainer ModifiedBlockTags = BlockTags;
	FGameplayTagContainer ModifiedCancelTags = CancelTags;

	if (TagRelationshipMapping)
	{
		// 通过 Mapping 表查询 → 将配置的 Block/Cancel 关系注入
		TagRelationshipMapping->GetAbilityTagsToBlockAndCancel(AbilityTags, &ModifiedBlockTags, &ModifiedCancelTags);
	}

	// 调用父类，使用扩展后的 Block/Cancel 标签
	Super::ApplyAbilityBlockAndCancelTags(AbilityTags, RequestingAbility,
		bEnableBlockTags, ModifiedBlockTags, bExecuteCancelTags, ModifiedCancelTags);
}

// 根据技能标签集合，从 Mapping 中查询额外的激活必要/阻止条件。
// 在技能激活前由引擎内部调用，将 Mapping 配置的条件合并到 ActivationRequiredTags / ActivationBlockedTags。
void UDark_TdoreAbilitySystemComponent::GetAdditionalActivationTagRequirements(
	const FGameplayTagContainer& AbilityTags,
	FGameplayTagContainer& OutActivationRequired,
	FGameplayTagContainer& OutActivationBlocked) const
{
	if (TagRelationshipMapping)
	{
		TagRelationshipMapping->GetRequiredAndBlockedActivationTags(AbilityTags, &OutActivationRequired, &OutActivationBlocked);
	}
}

// 设置标签关系映射表（通常在 PawnData/ASC 初始化时调用）
// 传入 nullptr 会清除映射
void UDark_TdoreAbilitySystemComponent::SetTagRelationshipMapping(
	UDark_TdoreAbilityTagRelationshipMapping* NewMapping)
{
	TagRelationshipMapping = NewMapping;
}
