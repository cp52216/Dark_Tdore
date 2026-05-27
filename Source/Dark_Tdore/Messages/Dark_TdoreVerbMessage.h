// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "Dark_TdoreVerbMessage.generated.h"

/**
 * FDark_TdoreVerbMessage — 跨系统事件消息（参考 Lyra FLyraVerbMessage）
 *
 * 统一格式：Instigator Verb Target (in Context, with Magnitude)
 *
 * 典型用法：
 *   Damage → { Instigator=PlayerA, Verb="Gameplay.Damage.Message", Target=PlayerB, Magnitude=50 }
 *   Killed  → { Instigator=PlayerA, Verb="Gameplay.Killed", Target=PlayerB }
 *   Healed  → { Instigator=PlayerA, Verb="Gameplay.Healed", Target=PlayerB, Magnitude=30 }
 *
 * 发送方：HealthSet / HealthComponent 等系统内部
 * 接收方：UI、KillFeed、成就系统、Debugger — 通过 UGameplayMessageSubsystem 订阅
 */
USTRUCT(BlueprintType)
struct FDark_TdoreVerbMessage
{
	GENERATED_BODY()

	/** 事件动词（如 "Gameplay.Damage.Message", "Gameplay.Killed"） */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	FGameplayTag Verb;

	/** 事件发起者（如造成伤害的玩家） */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	TObjectPtr<UObject> Instigator = nullptr;

	/** 事件目标（如受到伤害的玩家） */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	TObjectPtr<UObject> Target = nullptr;

	/** 发起者标签（如 "Team.Red", "Status.Invisible"） */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	FGameplayTagContainer InstigatorTags;

	/** 目标标签 */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	FGameplayTagContainer TargetTags;

	/** 上下文标签（如 "Damage.Critical", "Damage.Headshot"） */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	FGameplayTagContainer ContextTags;

	/** 量级（伤害值、治疗量等） */
	UPROPERTY(BlueprintReadWrite, Category = Gameplay)
	double Magnitude = 1.0;

	/** 调试字符串 */
	DARK_TDORE_API FString ToString() const;
};
