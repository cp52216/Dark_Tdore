// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "Dark_TdoreAttributeSet.h"

#include "Dark_TdoreCombatSet.generated.h"

/**
 * UDark_TdoreCombatSet — 战斗属性集（参考 Lyra ULyraCombatSet）
 *
 * 定义伤害/治疗计算所需的基础数值——由 GE（装备、Buff）修改，
 * ExecutionCalculation 读取这些值代入最终计算。
 *
 * 属性：
 *   BaseDamage  — 基础伤害值（武器面板、技能系数等）
 *   BaseHeal    — 基础治疗值
 *
 * 典型管线：
 *   武器 GE 写入 BaseDamage = 50 → Buff GE 乘 1.5 → BaseDamage = 75
 *   → GA 激活 → ExecutionCalculation 读取 BaseDamage → 计算最终伤害 → 写入 HealthSet::Damage
 */
UCLASS(BlueprintType)
class UDark_TdoreCombatSet : public UDark_TdoreAttributeSet
{
	GENERATED_BODY()

public:
	UDark_TdoreCombatSet();

	ATTRIBUTE_ACCESSORS(UDark_TdoreCombatSet, BaseDamage);
	ATTRIBUTE_ACCESSORS(UDark_TdoreCombatSet, BaseHeal);
	ATTRIBUTE_ACCESSORS(UDark_TdoreCombatSet, Mana);
	ATTRIBUTE_ACCESSORS(UDark_TdoreCombatSet, MaxMana);

protected:
	UFUNCTION()
	void OnRep_BaseDamage(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_BaseHeal(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Mana(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxMana(const FGameplayAttributeData& OldValue);

private:
	/** 基础伤害值（ExecutionCalculation 读取此值计算最终伤害） */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BaseDamage, Category = "DarkTdore|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BaseDamage;

	/** 基础治疗值 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BaseHeal, Category = "DarkTdore|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BaseHeal;

	/** 当前法力值（技能消耗） */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Mana, Category = "DarkTdore|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Mana;

	/** 最大法力值 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxMana, Category = "DarkTdore|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxMana;
};
