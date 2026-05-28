// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGameplayAbility_Interact.h"
#include "AbilitySystemComponent.h"
#include "Interaction/Dark_TdoreInteractableTarget.h"
#include "Interaction/Dark_TdoreInteractionStatics.h"
#include "Interaction/Tasks/AbilityTask_GrantNearbyInteraction.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGameplayAbility_Interact)

// ============ GameplayTag 定义 ============

/** 交互持续时间消息 Tag — 通过 GameplayMessageSubsystem 广播 */
UE_DEFINE_GAMEPLAY_TAG(TAG_INTERACTION_DURATION_MESSAGE, "Ability.Interaction.Duration.Message");

// ============ 构造 ============

UDark_TdoreGameplayAbility_Interact::UDark_TdoreGameplayAbility_Interact(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// OnSpawn: 角色生成时自动激活（不需要按键触发）
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnSpawn;

	// InstancedPerActor: 每个使用此 GA 的角色创建独立实例
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// LocalPredicted: 客户端预测执行（交互操作需要低延迟）
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

// ============ ActivateAbility ============
// 角色生成时引擎自动调用（因为 ActivationPolicy = OnSpawn）
//
// 流程：
//   1. Super::ActivateAbility → 触发父类逻辑（CommitAbility 等）
//   2. 仅在服务器创建 GrantNearbyInteraction Task
//   3. Task::ReadyForActivation → 启动 Timer 扫描

void UDark_TdoreGameplayAbility_Interact::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Step 1: 父类激活（执行 CommitAbility、ApplyCost 等）
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Step 2: 仅在服务器创建扫描 Task
	// 交互 GA 的授予逻辑需要 GiveAbility，只能由服务器执行
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (AbilitySystem && AbilitySystem->GetOwnerRole() == ROLE_Authority)
	{
		UE_LOG(LogDark_TdoreGAS, Display, TEXT("[Interaction] 交互 GA 已激活: %s (范围=%.0fcm, 频率=%.2fs)"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), InteractionScanRange, InteractionScanRate);

		// Step 3: 创建扫描 Task 并立即激活
		UAbilityTask_GrantNearbyInteraction* Task =
			UAbilityTask_GrantNearbyInteraction::GrantAbilitiesForNearbyInteractors(
				this, InteractionScanRange, InteractionScanRate);

		// ReadyForActivation 触发 Task::Activate() → 启动 Timer
		Task->ReadyForActivation();
	}
}
