// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Interaction/Dark_TdoreInteractionQuery.h"
#include "AbilityTask_WaitForInteractableTargets.h"
#include "AbilityTask_WaitForInteractableTargets_SingleLineTrace.generated.h"

/**
 * UAbilityTask_WaitForInteractableTargets_SingleLineTrace — 单线射线交互检测（参考 Lyra）
 *
 * 从摄像机发射射线，检测准心对准的交互目标。
 * 适用于精确瞄准场景（FPS/TPS 准心交互），与 GrantNearbyInteraction（球形扫描）互补。
 *
 * 工作流程：
 *   Timer → PerformTrace → AimWithPlayerController → LineTrace → 提取交互目标
 *     → UpdateInteractableOptions → 过滤可激活选项 → 通知 InteractableObjectsChanged Delegate
 *
 * 可选 Debug 可视化：
 *   bShowDebug = true → 画红/绿调试线（红=命中交互目标，绿=未命中）
 */
UCLASS()
class UAbilityTask_WaitForInteractableTargets_SingleLineTrace : public UAbilityTask_WaitForInteractableTargets
{
	GENERATED_UCLASS_BODY()

	virtual void Activate() override;

	/**
	 * 创建单线射线交互检测 Task
	 * @param OwningAbility       所属 GA
	 * @param InteractionQuery    查询上下文（谁在查询）
	 * @param TraceProfile        碰撞 Profile（如 "Pawn", "Visibility"）
	 * @param StartLocation       射线起点（通常为角色位置）
	 * @param InteractionScanRange 最大检测距离（厘米）
	 * @param InteractionScanRate  扫描频率（秒）
	 * @param bShowDebug          是否画调试线
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
		meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_WaitForInteractableTargets_SingleLineTrace* WaitForInteractableTargets_SingleLineTrace(
		UGameplayAbility* OwningAbility, FDark_TdoreInteractionQuery InteractionQuery,
		FCollisionProfileName TraceProfile, FGameplayAbilityTargetingLocationInfo StartLocation,
		float InteractionScanRange = 500, float InteractionScanRate = 0.1f, bool bShowDebug = false);

private:
	virtual void OnDestroy(bool AbilityEnded) override;

	/** 执行一次射线扫描（由 Timer 触发） */
	void PerformTrace();

	// ===== 配置参数 =====
	FDark_TdoreInteractionQuery InteractionQuery;
	FGameplayAbilityTargetingLocationInfo StartLocation;
	float InteractionScanRange = 500;
	float InteractionScanRate = 0.1f;
	bool bShowDebug = false;

	// ===== Timer =====
	FTimerHandle TimerHandle;
};
