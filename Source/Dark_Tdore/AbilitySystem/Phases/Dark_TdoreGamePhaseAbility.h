// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Dark_TdoreGameplayAbility.h"
#include "Dark_TdoreGamePhaseAbility.generated.h"

/**
 * UDark_TdoreGamePhaseAbility — 游戏阶段能力基类（参考 Lyra ULyraGamePhaseAbility）
 *
 * 每个游戏阶段（如 "Game.PreGame", "Game.Playing", "Game.PostGame"）
 * 都是一个 GA，挂载在 GameState 的 ASC 上。
 *
 * 阶段间关系（父子/兄弟）通过 GameplayTag 层级自动管理：
 *
 *   GamePhase
 *   ├─ PreGame        ← 父阶段
 *   ├─ Playing        ← 父阶段
 *   │   ├─ WarmUp     ← 子阶段（Playing 仍活跃）
 *   │   └─ SuddenDeath← 子阶段（自动取消 WarmUp）
 *   └─ PostGame       ← 父阶段（自动取消所有 Playing*）
 *
 * 规则：
 *   - 启动新阶段 → 自动取消所有非祖先的活跃阶段
 *   - 父阶段可和子阶段共存（Game.Playing + Game.Playing.WarmUp ✅）
 *   - 兄弟阶段不能共存（WarmUp 和 SuddenDeath 不能同时活跃）
 */
UCLASS(Abstract, HideCategories = Input)
class UDark_TdoreGamePhaseAbility : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UDark_TdoreGamePhaseAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 获取本阶段关联的 GameplayTag */
	const FGameplayTag& GetGamePhaseTag() const { return GamePhaseTag; }

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/**
	 * 本阶段对应的 GameplayTag
	 *
	 * 示例：
	 *   GamePhase.PreGame       — 等待玩家阶段
	 *   GamePhase.Playing       — 正式游戏
	 *   GamePhase.Playing.WarmUp — 热身子阶段
	 *   GamePhase.PostGame      — 结算阶段
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GamePhase")
	FGameplayTag GamePhaseTag;
};
