// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectExecutionCalculation.h"

#include "Dark_TdoreDamageExecution.generated.h"

class UObject;

/**
 * UDark_TdoreDamageExecution — 伤害 ExecutionCalculation（参考 Lyra ULyraDamageExecution）
 *
 * 替代简单的 GE Modifier 扣血方式。ExecutionCalculation 的优势：
 *   1. 可以读取多个 AttributeSet（攻击者的 CombatSet、受击者的 HealthSet）
 *   2. 可以访问 GameplayEffectContext 中的扩展数据（HitResult、物理材质等）
 *   3. 复杂公式集中管理，便于调参和平衡
 *
 * 计算公式：
 *   FinalDamage = Source.BaseDamage × DistanceAttenuation × DamageMultiplier
 *
 * 未来扩展点（注释标记）：
 *   - ArmorReduction：读取 Target.DefenseSet.Armor 做减伤
 *   - CritMultiplier：暴击倍率
 *   - TeamCheck：队伍伤害检查（需要 TeamSystem）
 *
 * 管线：
 *   GE_Damage（Execution: DamageExecution）
 *     → Execute_Implementation:
 *        读取 Source.CombatSet.BaseDamage
 *        计算 FinalDamage
 *        → Output: Target.HealthSet.Damage += FinalDamage
 *     → HealthSet::PostGameplayEffectExecute:
 *        Health -= Damage → 广播 OnHealthChanged
 *        Health≤0 → OnOutOfHealth → GA_Death
 */
UCLASS()
class UDark_TdoreDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UDark_TdoreDamageExecution();

protected:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
