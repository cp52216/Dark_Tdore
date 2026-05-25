// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dark_TdoreAbilityCost.h"
#include "ScalableFloat.h"

#include "Dark_TdoreAbilityCost_Attribute.generated.h"

struct FGameplayAbilityActivationInfo;
struct FGameplayAbilitySpecHandle;
class UDark_TdoreGameplayAbility;
struct FGameplayAbilityActorInfo;
struct FGameplayAttribute;

/**
 * UDark_TdoreAbilityCost_Attribute — 属性消耗（法力、耐力等）
 *
 * 从角色的 AttributeSet 中扣除指定属性值作为技能消耗。
 * 典型用法：法力消耗、耐力消耗。
 *
 * 配置：
 *   - Attribute: 要消耗的属性（如 HealthSet.Mana）
 *   - Amount: 每级消耗量（支持等级曲线，如 Level1=10, Level2=15...）
 *   - FailureTag: 支付失败时添加到 OptionalRelevantTags 的标签
 */
UCLASS(meta = (DisplayName = "Attribute Cost"))
class UDark_TdoreAbilityCost_Attribute : public UDark_TdoreAbilityCost
{
	GENERATED_BODY()

public:
	UDark_TdoreAbilityCost_Attribute();

	//~UDark_TdoreAbilityCost interface
	virtual bool CheckCost(const UDark_TdoreGameplayAbility* Ability,
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ApplyCost(const UDark_TdoreGameplayAbility* Ability,
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	//~End of UDark_TdoreAbilityCost interface

protected:
	/** 要消耗的属性（如 "Dark_TdoreCombatSet.Mana"） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Costs)
	FGameplayAttribute Attribute;

	/** 每级消耗量（支持等级曲线：Level1=10, Level2=15...） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Costs)
	FScalableFloat Amount;

	/** 资源不足时返回的失败标签（UI 可监听此标签显示"法力不足"等提示） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Costs)
	FGameplayTag FailureTag;
};
