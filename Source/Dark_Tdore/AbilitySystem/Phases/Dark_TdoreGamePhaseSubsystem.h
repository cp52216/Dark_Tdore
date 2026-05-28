// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "Dark_TdoreGamePhaseSubsystem.generated.h"

class UDark_TdoreGamePhaseAbility;

DECLARE_DYNAMIC_DELEGATE_OneParam(FDarkTdoreGamePhaseDynamicDelegate, const UDark_TdoreGamePhaseAbility*, Phase);
DECLARE_DELEGATE_OneParam(FDarkTdoreGamePhaseDelegate, const UDark_TdoreGamePhaseAbility* Phase);

DECLARE_DYNAMIC_DELEGATE_OneParam(FDarkTdoreGamePhaseTagDynamicDelegate, const FGameplayTag&, PhaseTag);
DECLARE_DELEGATE_OneParam(FDarkTdoreGamePhaseTagDelegate, const FGameplayTag& PhaseTag);

/** 阶段 Tag 匹配模式（参考 Lyra EPhaseTagMatchType） */
UENUM(BlueprintType)
enum class EPhaseTagMatchType : uint8
{
	ExactMatch,    // 精确匹配：订阅 "Game.Playing" 只匹配 Game.Playing
	PartialMatch   // 部分匹配：订阅 "Game.Playing" 同时匹配 Game.Playing.WarmUp
};

/**
 * UDark_TdoreGamePhaseSubsystem — 游戏阶段管理子系统（参考 Lyra ULyraGamePhaseSubsystem）
 *
 * UWorldSubsystem，每个 World 一个实例。
 *
 * 核心设计：用 GameplayTag 层级代替状态机
 *   - 父阶段和子阶段可以共存：Game.Playing + Game.Playing.WarmUp ✅
 *   - 兄弟阶段互斥：启动 SuddenDeath 自动取消 WarmUp
 *   - 非祖先阶段自动取消：启动 PostGame 自动取消所有 Playing*
 *
 * 典型用法：
 *   GameState 的 ASC 上挂 Phase Ability → StartPhase → 子阶段自动管理
 *   → 其他系统通过 WhenPhaseStartsOrIsActive 监听阶段切换
 */
UCLASS()
class UDark_TdoreGamePhaseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ===== USubsystem =====
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// ===== 阶段管理 =====

	/**
	 * 启动一个游戏阶段
	 * @param PhaseAbility      阶段能力蓝图子类（如 BP_Phase_Playing）
	 * @param PhaseEndedCallback 阶段结束时的回调
	 */
	void StartPhase(TSubclassOf<UDark_TdoreGamePhaseAbility> PhaseAbility,
		FDarkTdoreGamePhaseDelegate PhaseEndedCallback = FDarkTdoreGamePhaseDelegate());

	/** 蓝图版 StartPhase */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GamePhase", meta = (DisplayName = "Start Phase"))
	void K2_StartPhase(TSubclassOf<UDark_TdoreGamePhaseAbility> Phase,
		const FDarkTdoreGamePhaseDynamicDelegate& PhaseEnded);

	// ===== 观察者（阶段生命周期监听）=====

	/** 当指定阶段启动或已处于活跃状态时回调 */
	void WhenPhaseStartsOrIsActive(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType,
		const FDarkTdoreGamePhaseTagDelegate& WhenPhaseActive);

	/** 当指定阶段结束时回调 */
	void WhenPhaseEnds(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType,
		const FDarkTdoreGamePhaseTagDelegate& WhenPhaseEnd);

	/** 蓝图版 WhenPhaseStartsOrIsActive */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GamePhase")
	void K2_WhenPhaseStartsOrIsActive(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType,
		FDarkTdoreGamePhaseTagDynamicDelegate WhenPhaseActive);

	/** 蓝图版 WhenPhaseEnds */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GamePhase")
	void K2_WhenPhaseEnds(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType,
		FDarkTdoreGamePhaseTagDynamicDelegate WhenPhaseEnd);

	/** 查询指定阶段是否正在活跃 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, BlueprintPure = false)
	bool IsPhaseActive(const FGameplayTag& PhaseTag) const;

protected:
	// 由 UDark_TdoreGamePhaseAbility 的 Activate/End 回调调用
	void OnBeginPhase(const UDark_TdoreGamePhaseAbility* PhaseAbility, const FGameplayAbilitySpecHandle Handle);
	void OnEndPhase(const UDark_TdoreGamePhaseAbility* PhaseAbility, const FGameplayAbilitySpecHandle Handle);

	friend class UDark_TdoreGamePhaseAbility;

private:
	// ===== 内部数据结构 =====

	/** 活跃阶段条目 */
	struct FGamePhaseEntry
	{
		FGameplayTag PhaseTag;
		FDarkTdoreGamePhaseDelegate PhaseEndedCallback;
	};

	/** 阶段观察者 */
	struct FPhaseObserver
	{
		bool IsMatch(const FGameplayTag& ComparePhaseTag) const;

		FGameplayTag PhaseTag;
		EPhaseTagMatchType MatchType = EPhaseTagMatchType::ExactMatch;
		FDarkTdoreGamePhaseTagDelegate PhaseCallback;
	};

	// ===== 成员变量 =====

	/** Handle → 活跃阶段 映射表 */
	TMap<FGameplayAbilitySpecHandle, FGamePhaseEntry> ActivePhaseMap;

	/** 阶段启动观察者列表（永不清理，直到 World 重置） */
	TArray<FPhaseObserver> PhaseStartObservers;

	/** 阶段结束观察者列表 */
	TArray<FPhaseObserver> PhaseEndObservers;
};
