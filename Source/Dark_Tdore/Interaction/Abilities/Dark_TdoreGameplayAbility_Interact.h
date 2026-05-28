// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Dark_TdoreGameplayAbility.h"
#include "Interaction/Dark_TdoreInteractionOption.h"
#include "Dark_TdoreGameplayAbility_Interact.generated.h"

/**
 * UDark_TdoreGameplayAbility_Interact — 交互能力基类（参考 Lyra ULyraGameplayAbility_Interact）
 *
 * 激活策略：OnSpawn — 角色生成时自动激活并持续运行。
 *
 * 激活时创建 GrantNearbyInteraction Task：
 *   → 定时球形扫描角色周围
 *   → 发现 IInteractableTarget → 收集交互选项
 *   → 自动授予交互 GA 给角色的 ASC
 *
 * 配置属性（蓝图可覆写）：
 *   InteractionScanRange = 500  → 扫描半径（厘米）
 *   InteractionScanRate  = 0.1  → 扫描频率（秒）
 *
 * 使用方式：
 *   DA_DefaultAbilitySet → 添加此 GA（OnSpawn 自动激活）
 *   → 角色周围有可交互目标时自动获得交互 GA
 */
UCLASS()
class UDark_TdoreGameplayAbility_Interact : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UDark_TdoreGameplayAbility_Interact(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * 激活时创建 GrantNearbyInteraction Task 开始扫描
	 * 仅在服务器执行（ROLE_Authority）
	 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	/** 扫描范围（厘米），默认 500cm = 5 米 */
	UPROPERTY(EditDefaultsOnly)
	float InteractionScanRange = 500;

	/** 扫描频率（秒），默认 0.1s = 每秒扫描 10 次 */
	UPROPERTY(EditDefaultsOnly)
	float InteractionScanRate = 0.1f;
};
