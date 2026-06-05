// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Dark_TdoreCombatTypes.generated.h"

class UAnimMontage;
class UCurveFloat;
class UGameplayEffect;

UENUM(BlueprintType)
enum class EDark_TdoreCombatAttackWeight : uint8
{
	// 轻攻击：连段起手、普通派生、较低硬直。
	Light,
	// 中攻击：预留给以后介于轻/重之间的武器招式。
	Medium,
	// 重攻击：通常用于更高削韧、破防反馈或更强后摇。
	Heavy,
	// 投技：与普通打击分开，方便以后走抓取/处决判定。
	Throw
};

UENUM(BlueprintType)
enum class EDark_TdoreCombatImpactLevel : uint8
{
	// 无受击反馈，例如仅触发特效或命中无硬直目标。
	None,
	// 轻硬直。
	Light,
	// 中硬直。
	Medium,
	// 重硬直。
	Heavy,
	// 击飞/浮空。
	Launch
};

USTRUCT(BlueprintType)
struct FDark_TdoreCombatMovementConfig
{
	GENERATED_BODY()

	// 本段攻击期望推进的总距离。实际速度应由曲线采样，不建议直接匀速推进。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float Distance = 0.0f;

	// 推进持续时间，通常与动画通知窗口或蒙太奇片段长度匹配。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float Duration = 0.0f;

	// 速度权重曲线。后续实现位移时应对曲线做归一化/积分，保证配置距离可控。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	TObjectPtr<UCurveFloat> SpeedCurve;
};

USTRUCT(BlueprintType)
struct FDark_TdoreAttackMagnetismConfig
{
	GENERATED_BODY()

	// 是否启用攻击吸附。第一版只存数据，后续可接 Motion Warping。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magnetism")
	bool bEnableMagnetism = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magnetism", meta = (EditCondition = "bEnableMagnetism"))
	float MinDistance = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magnetism", meta = (EditCondition = "bEnableMagnetism"))
	float MaxDistance = 350.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magnetism", meta = (EditCondition = "bEnableMagnetism"))
	FName MotionWarpingTargetName = TEXT("AttackTarget");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magnetism", meta = (EditCondition = "bEnableMagnetism"))
	TObjectPtr<UCurveFloat> WeightCurve;
};

USTRUCT(BlueprintType)
struct FDark_TdoreComboStep
{
	GENERATED_BODY()

	// 连招段唯一名字，例如 Light_01、Light_02、Heavy_Finisher。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo")
	FName StepName;

	// 本段播放的攻击蒙太奇。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo")
	TObjectPtr<UAnimMontage> Montage;

	// 可选：同一个 Montage 内的 Section 名。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo")
	FName MontageSection;

	// 触发本段所需输入。起手段也可以填 Light/Heavy，用于数据查询。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo", meta = (Categories = "InputTag"))
	FGameplayTag InputTag;

	// 当前段在预输入窗口内允许派生的下一段输入集合。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo", meta = (Categories = "InputTag"))
	FGameplayTagContainer AllowedNextInputTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	EDark_TdoreCombatAttackWeight AttackWeight = EDark_TdoreCombatAttackWeight::Light;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	EDark_TdoreCombatImpactLevel ImpactLevel = EDark_TdoreCombatImpactLevel::Light;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	FGameplayTagContainer AttackTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack")
	TSubclassOf<UGameplayEffect> DamageEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	FDark_TdoreCombatMovementConfig Movement;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magnetism")
	FDark_TdoreAttackMagnetismConfig Magnetism;
};

UCLASS(BlueprintType, Const)
class UDark_TdoreComboData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	bool FindStepForInputTag(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	bool FindStepByName(FName StepName, FDark_TdoreComboStep& OutStep) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	const TArray<FDark_TdoreComboStep>& GetSteps() const { return Steps; }

private:
	// 由武器/技能引用的数据表。设计师只改这里，不需要改 Character 或 Ability C++。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combo", meta = (AllowPrivateAccess = "true", TitleProperty = "StepName"))
	TArray<FDark_TdoreComboStep> Steps;
};
