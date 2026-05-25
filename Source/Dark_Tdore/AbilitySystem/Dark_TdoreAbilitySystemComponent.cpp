// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAbilitySystemComponent.h"
#include "Dark_TdoreGameplayAbility.h"
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
