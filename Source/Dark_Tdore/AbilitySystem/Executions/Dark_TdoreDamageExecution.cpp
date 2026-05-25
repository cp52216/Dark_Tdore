// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreDamageExecution.h"
#include "AbilitySystem/Attributes/Dark_TdoreHealthSet.h"
#include "AbilitySystem/Attributes/Dark_TdoreCombatSet.h"
#include "AbilitySystem/Dark_TdoreGameplayEffectContext.h"
#include "Dark_TdoreLogChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreDamageExecution)

// ============ 属性捕获静态缓存 ============

struct FDamageStatics
{
	// 从攻击者（Source）捕获 BaseDamage — 即 "我出了多少伤害"
	FGameplayEffectAttributeCaptureDefinition BaseDamageDef;

	FDamageStatics()
	{
		BaseDamageDef = FGameplayEffectAttributeCaptureDefinition(
			UDark_TdoreCombatSet::GetBaseDamageAttribute(),
			EGameplayEffectAttributeCaptureSource::Source,  // 从 GE 的 Source（攻击者）读取
			true                                              // 快照：捕获 GE 应用时的快照值
		);
	}
};

static FDamageStatics& DamageStatics()
{
	static FDamageStatics Statics;
	return Statics;
}

// ============ 构造：注册需要捕获的属性 ============

UDark_TdoreDamageExecution::UDark_TdoreDamageExecution()
{
	RelevantAttributesToCapture.Add(DamageStatics().BaseDamageDef);
}

// ============ 执行伤害计算 ============

void UDark_TdoreDamageExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
#if WITH_SERVER_CODE
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	// 提取我们的扩展 EffectContext
	FDark_TdoreGameplayEffectContext* TypedContext = FDark_TdoreGameplayEffectContext::ExtractEffectContext(Spec.GetContext());
	check(TypedContext);

	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = SourceTags;
	EvaluateParameters.TargetTags = TargetTags;

	// ===== Step 1: 获取攻击者的基础伤害 =====
	float BaseDamage = 0.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().BaseDamageDef, EvaluateParameters, BaseDamage);

	UE_LOG(LogDark_TdoreGAS, Log,
		TEXT("[DamageExecution] Source.BaseDamage = %.1f | 攻击者: %s"),
		BaseDamage,
		*GetNameSafe(TypedContext->GetEffectCauser()));

	// ===== Step 2: 伤害衰减计算（距离衰减/物理材质衰减）=====
	const AActor* EffectCauser = TypedContext->GetEffectCauser();
	const FHitResult* HitActorResult = TypedContext->GetHitResult();

	AActor* HitActor = nullptr;
	FVector ImpactLocation = FVector::ZeroVector;

	if (HitActorResult)
	{
		HitActor = HitActorResult->HitObjectHandle.FetchActor();
		if (HitActor)
		{
			ImpactLocation = HitActorResult->ImpactPoint;
		}
	}

	// 无 HitResult 时退化到目标 Actor 位置
	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	if (!HitActor)
	{
		HitActor = TargetASC ? TargetASC->GetAvatarActor_Direct() : nullptr;
		if (HitActor)
		{
			ImpactLocation = HitActor->GetActorLocation();
		}
	}

	// TODO: 距离衰减（需要 AbilitySourceInterface）
	double Distance = WORLD_MAX;
	if (EffectCauser && HitActor)
	{
		Distance = FVector::Dist(EffectCauser->GetActorLocation(), ImpactLocation);
	}

	const float DistanceAttenuation = 1.0f;   // TODO: 接入 AbilitySourceInterface 后从接口获取
	const float PhysicalMaterialAttenuation = 1.0f; // TODO: 同上

	// TODO: 队伍伤害检查
	const float DamageInteractionAllowedMultiplier = 1.0f; // TODO: 接入 TeamSubsystem 后检查

	// ===== Step 3: 计算最终伤害 =====
	const float DamageDone = FMath::Max(
		BaseDamage * DistanceAttenuation * PhysicalMaterialAttenuation * DamageInteractionAllowedMultiplier,
		0.0f);

	if (DamageDone > 0.0f)
	{
		UE_LOG(LogDark_TdoreGAS, Log,
			TEXT("[DamageExecution] 最终伤害: %.1f → %s"),
			DamageDone,
			*GetNameSafe(HitActor));

		// 写入 Damage 元属性 → HealthSet::PostGameplayEffectExecute 将其转换为 Health 减少
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UDark_TdoreHealthSet::GetDamageAttribute(),
			EGameplayModOp::Additive,
			DamageDone));
	}
#endif // WITH_SERVER_CODE
}
