// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreHealExecution.h"
#include "AbilitySystem/Attributes/Dark_TdoreHealthSet.h"
#include "AbilitySystem/Attributes/Dark_TdoreCombatSet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreHealExecution)

// ============ 属性捕获静态缓存 ============

struct FHealStatics
{
	FGameplayEffectAttributeCaptureDefinition BaseHealDef;

	FHealStatics()
	{
		BaseHealDef = FGameplayEffectAttributeCaptureDefinition(
			UDark_TdoreCombatSet::GetBaseHealAttribute(),
			EGameplayEffectAttributeCaptureSource::Source,
			true
		);
	}
};

static FHealStatics& HealStatics()
{
	static FHealStatics Statics;
	return Statics;
}

// ============ 构造 ============

UDark_TdoreHealExecution::UDark_TdoreHealExecution()
{
	RelevantAttributesToCapture.Add(HealStatics().BaseHealDef);
}

// ============ 执行治疗计算 ============

void UDark_TdoreHealExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
#if WITH_SERVER_CODE
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = SourceTags;
	EvaluateParameters.TargetTags = TargetTags;

	float BaseHeal = 0.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(HealStatics().BaseHealDef, EvaluateParameters, BaseHeal);

	const float HealingDone = FMath::Max(0.0f, BaseHeal);

	if (HealingDone > 0.0f)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UDark_TdoreHealthSet::GetHealingAttribute(),
			EGameplayModOp::Additive,
			HealingDone));
	}
#endif // WITH_SERVER_CODE
}
