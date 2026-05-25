// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dark_TdoreGameplayAbility.h"
#include "GA_TestQ.generated.h"

/**
 * UGA_TestQ — Q 键测试技能
 *
 * 用于验证完整 GAS 管线的测试技能：
 *   Q 按键 → Character::OnAbilityQPressed()
 *         → ASC::AbilityInputTagPressed("Input.Ability.Q")
 *         → Tick::ProcessAbilityInput()
 *         → TryActivateAbility(GA_TestQ)
 *         → GA_TestQ::ActivateAbility() → 打印日志 + 屏幕消息
 *
 * 验证要点：
 *   1. 输入路由: Q 按键是否能正确触发
 *   2. ASC 缓冲: 输入是否被正确缓冲和处理
 *   3. 技能激活: GA_TestQ 是否成功激活
 *   4. 日志输出: 控制台是否打印完整激活信息
 *   5. 屏幕消息: 游戏视图是否显示激活消息
 *
 * 激活后会在控制台日志输出：
 *   ================================================
 *   GA_TestQ ACTIVATED! Q key press detected.
 *     ASC: Dark_TdoreAbilitySystemComponent
 *     Avatar: BP_ThirdPersonCharacter_C_0
 *   ================================================
 */
UCLASS()
class UGA_TestQ : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_TestQ();

protected:
	//~UGameplayAbility 接口覆写
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** 激活时显示的屏幕消息文本（可在蓝图中自定义） */
	UPROPERTY(EditDefaultsOnly, Category = "Test")
	FText ActivationMessage;

	/** 冷却时间（秒），技能激活后在此时间后自动结束 */
	UPROPERTY(EditDefaultsOnly, Category = "Test")
	float CooldownDuration = 2.0f;

private:
	/** 冷却计时器句柄 */
	FTimerHandle CooldownTimerHandle;
};
