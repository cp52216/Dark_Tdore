// Copyright Epic Games, Inc. All Rights Reserved.

#include "GA_Death.h"
#include "Character/Dark_TdoreHealthComponent.h"
#include "AbilitySystemComponent.h"
#include "Dark_TdoreAbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GA_Death)

// ============ 构造 ============

UGA_Death::UGA_Death(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 激活策略：不是按键激活，而是通过 GameplayEvent 触发
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;

	// 死亡后阻止所有其他技能
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Exclusive_Blocking;

	// 每个角色一个实例
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 仅服务端激活，客户端通过 DeathState 复制同步
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// 参考 Lyra：注册 GameplayEvent.Death 触发器
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FAbilityTriggerData TriggerData;
		TriggerData.TriggerTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayEvent.Death"));
		TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(TriggerData);
	}
}

// ============ 技能激活 ============

void UGA_Death::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UDark_TdoreAbilitySystemComponent* DarkASC = GetDarkTdoreAbilitySystemComponentFromActorInfo();
	if (!DarkASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ===== 1. 取消所有其他技能（但保留 SurvivesDeath 标记的技能）=====
	{
		FGameplayTagContainer AbilityTypesToIgnore;
		AbilityTypesToIgnore.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Behavior.SurvivesDeath")));
		DarkASC->CancelAbilities(nullptr, &AbilityTypesToIgnore, this);
	}

	// ===== 2. 死亡技能本身不可被取消 =====
	SetCanBeCanceled(false);

	// ===== 3. 提交技能（标记为已激活，允许 EndAbility）=====
	CommitAbility(Handle, ActorInfo, ActivationInfo);

	// ===== 4. 自动开始死亡（如果 bAutoStartDeath）=====
	if (bAutoStartDeath)
	{
		StartDeath();
	}
}

// ============ 技能结束 ============

void UGA_Death::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 参考 Lyra：始终尝试 FinishDeath，如果死亡未开始则什么都不做
	FinishDeath();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ============ 死亡流程 ============

void UGA_Death::StartDeath()
{
	AActor* Owner = GetAvatarActorFromActorInfo();
	UDark_TdoreHealthComponent* HealthComponent = UDark_TdoreHealthComponent::FindHealthComponent(Owner);

	if (HealthComponent && HealthComponent->GetDeathState() == EDeathState::NotDead)
	{
		// 调用 HealthComponent 开始死亡 → DeathState = DeathStarted + Tag: Status.Death.Dying
		HealthComponent->StartDeath();

		// 通知蓝图：死亡已开始（蓝图层添加日志/动画等）
		K2_OnDeathStarted();
	}

	// ===== 延迟自动结束死亡技能 =====
	// 蓝图可在 K2_OnDeathStarted 中播放死亡动画，动画结束后调用 K2_EndAbility 或自动超时结束
	if (UWorld* World = GetWorld())
	{
		FTimerHandle DeathTimerHandle;
		World->GetTimerManager().SetTimer(DeathTimerHandle, [this]()
		{
			if (IsActive())
			{
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
			}
		}, 2.0f, false);
	}
}

void UGA_Death::FinishDeath()
{
	AActor* Owner = GetAvatarActorFromActorInfo();
	UDark_TdoreHealthComponent* HealthComponent = UDark_TdoreHealthComponent::FindHealthComponent(Owner);

	if (HealthComponent && HealthComponent->GetDeathState() == EDeathState::DeathStarted)
	{
		// 调用 HealthComponent 完成死亡 → DeathState = DeathFinished + Tag: Status.Death.Dead
		HealthComponent->FinishDeath();

		// 通知蓝图：死亡已完成（蓝图层添加日志/重生等）
		K2_OnDeathFinished();
	}
}

// ============ BlueprintNativeEvent 默认实现（蓝图可覆写） ============

void UGA_Death::K2_OnDeathStarted_Implementation()
{
}

void UGA_Death::K2_OnDeathFinished_Implementation()
{
}
