// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectExecutionCalculation.h"

#include "Dark_TdoreHealExecution.generated.h"

class UObject;

/**
 * UDark_TdoreHealExecution — 治疗 ExecutionCalculation（参考 Lyra ULyraHealExecution）
 *
 * 从 Source.CombatSet 读取 BaseHeal，写入 Target.HealthSet.Healing。
 * HealthSet::PostGameplayEffectExecute 会将 Healing 转换为 Health 增加。
 */
UCLASS()
class UDark_TdoreHealExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UDark_TdoreHealExecution();

protected:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
