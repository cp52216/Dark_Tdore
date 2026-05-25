// Copyright Epic Games, Inc. All Rights Reserved.

#include "GA_TestQ.h"
#include "Dark_TdoreLogChannels.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"

// ============ 构造 ============

UGA_TestQ::UGA_TestQ()
{
	// 按下按键时激活（OnInputTriggered）
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;
	// 独立运行，不与其他技能冲突
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Independent;

	// 激活时显示的屏幕消息（可在蓝图子类中修改文案）
	ActivationMessage = FText::FromString(TEXT("GA_TestQ 已激活！GAS 管线验证成功！"));
}

// ============ 技能激活 ============

void UGA_TestQ::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// ===== 控制台日志：打印完整激活信息 =====
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("================================================"));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("GA_TestQ 已激活！Q 键测试技能触发成功。"));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  ASC:    %s"), *GetNameSafe(ActorInfo->AbilitySystemComponent.Get()));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  Avatar: %s"), *GetNameSafe(ActorInfo->AvatarActor.Get()));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  Owner:  %s"), *GetNameSafe(ActorInfo->OwnerActor.Get()));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("================================================"));

	// ===== 屏幕消息：在游戏画面上显示激活提示 =====
	if (ActorInfo->AvatarActor.IsValid())
	{
		if (APlayerController* PC = ActorInfo->AvatarActor->GetInstigatorController<APlayerController>())
		{
			// ClientMessage 会在屏幕左上角显示黄色文字
			PC->ClientMessage(ActivationMessage.ToString());
		}
	}

	// ===== 定时器：技能激活后延迟自动结束 =====
	// 模拟一个瞬间技能（如打招呼、检查状态等）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(CooldownTimerHandle, [this, Handle, ActorInfo, ActivationInfo]()
		{
			// 如果技能仍在运行，结束它
			if (IsActive())
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			}
		}, CooldownDuration, false);  // false = 不循环
	}
}

// ============ 技能结束 ============

void UGA_TestQ::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	UE_LOG(LogDark_TdoreGAS, Display, TEXT("GA_TestQ 已结束。（是否被取消: %d）"), bWasCancelled);
}
