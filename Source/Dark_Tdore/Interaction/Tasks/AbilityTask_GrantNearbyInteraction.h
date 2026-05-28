// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_GrantNearbyInteraction.generated.h"

/**
 * UAbilityTask_GrantNearbyInteraction — 附近交互检测 + GA 授权（参考 Lyra UAbilityTask_GrantNearbyInteraction）
 *
 * 在 Interact GA 激活时创建，通过 Timer 定时扫描角色周围的交互目标：
 *
 *   1. 每 InteractionScanRate 秒执行一次 QueryInteractables
 *   2. OverlapMultiByChannel(ECC_WorldDynamic, Sphere) — 球形扫描
 *   3. AppendInteractableTargetsFromOverlapResults — 提取 IInteractableTarget
 *   4. GatherInteractionOptions — 收集交互选项
 *   5. GiveAbility — 自动授予交互 GA 给 ASC
 *
 * 典型配置：
 *   InteractionScanRange = 500cm → 5米范围
 *   InteractionScanRate  = 0.1s  → 每秒扫描 10 次
 *
 * 注意：
 *   - 仅服务器执行（ROLE_Authority）
 *   - 使用 InteractionAbilityCache 缓存已授予的 GA，避免重复 GiveAbility
 */
UCLASS()
class UAbilityTask_GrantNearbyInteraction : public UAbilityTask
{
	GENERATED_UCLASS_BODY()

	// ===== UAbilityTask 生命周期 =====

	/** 激活时启动 Timer（每 ScanRate 秒扫描一次） */
	virtual void Activate() override;

	/** 销毁时清理 Timer + 缓存 */
	virtual void OnDestroy(bool AbilityEnded) override;

	// ===== 静态工厂（蓝图不可直接调用，通过 C++ 创建）=====

	/**
	 * 创建一个扫描附近交互的 Task
	 * @param OwningAbility       所属的 GA（通常为 Interact GA）
	 * @param InteractionScanRange 扫描半径（厘米）
	 * @param InteractionScanRate  扫描频率（秒）
	 * @return 新创建的 Task 实例
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
		meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_GrantNearbyInteraction* GrantAbilitiesForNearbyInteractors(
		UGameplayAbility* OwningAbility, float InteractionScanRange, float InteractionScanRate);

private:
	// ===== 核心扫描逻辑 =====

	/**
	 * 执行一次扫描：
	 *   OverlapMultiByChannel → 提取 IInteractableTarget → 收集选项 → 授予 GA
	 */
	void QueryInteractables();

	// ===== 可配置参数 =====

	/** 扫描半径（厘米），默认 500cm = 5 米 */
	float InteractionScanRange = 500;

	/** 扫描频率（秒），默认 0.1s = 每秒 10 帧 */
	float InteractionScanRate = 0.1f;

	// ===== 内部状态 =====

	/** Timer 句柄（停止扫描时清理） */
	FTimerHandle QueryTimerHandle;

	/**
	 * 已授予 GA 的缓存
	 * Key: GA 类 (FObjectKey) → Value: AbilitySpecHandle
	 * 用于避免重复 GiveAbility（引擎不允许重复授予同一个 GA）
	 */
	TMap<FObjectKey, FGameplayAbilitySpecHandle> InteractionAbilityCache;
};
