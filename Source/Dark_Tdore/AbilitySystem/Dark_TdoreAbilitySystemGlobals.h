// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemGlobals.h"
#include "Dark_TdoreAbilitySystemGlobals.generated.h"

/**
 * UDark_TdoreAbilitySystemGlobals — 自定义 AbilitySystemGlobals（参考 Lyra ULyraAbilitySystemGlobals）
 *
 * 覆写 AllocGameplayEffectContext() 返回 FDark_TdoreGameplayEffectContext，
 * 使引擎的 ASC::MakeEffectContext() 自动创建扩展的 EffectContext。
 *
 * 此方式比覆写 ASC::MakeEffectContext() 更底层、更干净：
 *   引擎所有创建 EffectContext 的路径都会经过此工厂，包括：
 *     - ASC::MakeEffectContext()
 *     - GA::MakeEffectContext() → Super → ASC::MakeEffectContext()
 *     - ApplyGameplayEffectToSelf/Target
 *     - 蓝图 Apply Gameplay Effect 节点
 *
 * 配置：DefaultGame.ini
 *   [/Script/GameplayAbilities.AbilitySystemGlobals]
 *   AbilitySystemGlobalsClassName=/Script/Dark_Tdore.Dark_TdoreAbilitySystemGlobals
 */
UCLASS(Config = Game)
class UDark_TdoreAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_UCLASS_BODY()

	//~UAbilitySystemGlobals interface
	/** 返回自定义的 FDark_TdoreGameplayEffectContext，替代标准 FGameplayEffectContext */
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
	//~End of UAbilitySystemGlobals interface
};
