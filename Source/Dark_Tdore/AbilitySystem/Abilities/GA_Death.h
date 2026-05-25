// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dark_TdoreGameplayAbility.h"
#include "GA_Death.generated.h"

/**
 * UGA_Death — 死亡 GameplayAbility（参考 Lyra ULyraGameplayAbility_Death）
 *
 * 触发方式：不是按键激活，而是通过 AbilityTriggers 监听 GameplayEvent.Death 事件自动激活。
 *          当 HealthComponent::HandleOutOfHealth 发送 GameplayEvent.Death 时，
 *          ASC 自动匹配 TriggerTag 并激活此技能。
 *
 * 激活后流程：
 *   1. CancelAbilities — 取消所有其他技能（保留 Ability.Behavior.SurvivesDeath 标记的技能）
 *   2. SetCanBeCanceled(false) — 死亡技能本身不可被取消
 *   3. 切换激活组为 Exclusive_Blocking — 阻止任何新技能激活
 *   4. StartDeath → HealthComponent::StartDeath → 设置 Status.Death.Dying
 *   5. （蓝图控制死亡动画/布娃娃等）
 *   6. EndAbility → FinishDeath → HealthComponent::FinishDeath → 设置 Status.Death.Dead
 *
 * 关键属性：
 *   - InstancingPolicy = InstancedPerActor（每个角色一个实例）
 *   - NetExecutionPolicy = ServerInitiated（仅服务端触发，客户端通过 DeathState 复制同步）
 *   - bAutoStartDeath = true（默认立即开始死亡，蓝图可覆写为 false 以延迟）
 */
UCLASS(Blueprintable, Meta = (ShortTooltip = "死亡 GameplayAbility — 通过 GameplayEvent.Death 自动触发"))
class UGA_Death : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Death(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	//~UGameplayAbility 接口覆写
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * 开始死亡流程（调用 HealthComponent::StartDeath）
	 *
	 * 参考 Lyra：死亡为两阶段流程
	 *   StartDeath  → DeathState = DeathStarted  + Tag: Status.Death.Dying
	 *   FinishDeath → DeathState = DeathFinished + Tag: Status.Death.Dead
	 */
	UFUNCTION(BlueprintCallable, Category = "Death")
	void StartDeath();

	/**
	 * 完成死亡流程（调用 HealthComponent::FinishDeath）
	 *
	 * 在 EndAbility 中自动调用。蓝图可通过覆写 K2_OnDeathFinished 添加自定义逻辑。
	 */
	UFUNCTION(BlueprintCallable, Category = "Death")
	void FinishDeath();

	// ============ BlueprintNativeEvent 回调（C++ 默认打印日志，蓝图可覆写） ============

	/** 死亡开始后调用（C++ 默认打印屏幕消息，蓝图覆写可添加动画/布娃娃等） */
	UFUNCTION(BlueprintNativeEvent, Category = "Death", DisplayName = "OnDeathStarted")
	void K2_OnDeathStarted();
	virtual void K2_OnDeathStarted_Implementation();

	/** 死亡完成后调用（C++ 默认打印屏幕消息，蓝图覆写可添加延迟销毁/重生等） */
	UFUNCTION(BlueprintNativeEvent, Category = "Death", DisplayName = "OnDeathFinished")
	void K2_OnDeathFinished();
	virtual void K2_OnDeathFinished_Implementation();

protected:
	/**
	 * 是否在 ActivateAbility 中自动调用 StartDeath
	 *
	 * 默认 true（参考 Lyra）。设为 false 时蓝图需要手动调用 StartDeath，
	 * 适合需要在死亡前播放特殊动画/音效的场景。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	bool bAutoStartDeath = true;
};
