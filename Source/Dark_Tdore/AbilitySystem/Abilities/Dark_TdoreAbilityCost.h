// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "Dark_TdoreAbilityCost.generated.h"

class UDark_TdoreGameplayAbility;

/**
 * UDark_TdoreAbilityCost — 技能消耗基类（参考 Lyra ULyraAbilityCost）
 *
 * 所有消耗类型（法力、耐力、连击点、道具等）都继承此类。
 * 新增消耗类型只需加子类，不改 GA 代码。
 *
 * 子类需覆写：
 *   - CheckCost(): 检查是否有足够资源
 *   - ApplyCost(): 扣除资源
 *
 * 在 GA 蓝图的 AdditionalCosts 数组中添加子类实例即可生效。
 * 引擎自动在技能激活前调用 CheckCost，激活后调用 ApplyCost。
 */
UCLASS(MinimalAPI, DefaultToInstanced, EditInlineNew, Abstract)
class UDark_TdoreAbilityCost : public UObject
{
	GENERATED_BODY()

public:
	UDark_TdoreAbilityCost() {}

	/**
	 * 检查是否有足够资源支付此消耗
	 * @param Ability          正在激活的技能
	 * @param Handle           技能句柄
	 * @param ActorInfo        技能 Actor 信息
	 * @param OptionalRelevantTags [输出] 失败原因标签（如 "Ability.ActivateFail.Mana"）
	 * @return true 如果资源足够
	 */
	virtual bool CheckCost(const UDark_TdoreGameplayAbility* Ability,
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags) const
	{
		return true;
	}

	/**
	 * 实际扣除资源
	 * @param Ability          正在激活的技能
	 * @param Handle           技能句柄
	 * @param ActorInfo        技能 Actor 信息
	 * @param ActivationInfo   技能激活信息
	 */
	virtual void ApplyCost(const UDark_TdoreGameplayAbility* Ability,
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo)
	{
	}

	/** 仅在技能命中时才扣除 */
	bool ShouldOnlyApplyCostOnHit() const { return bOnlyApplyCostOnHit; }

protected:
	/** 仅在技能实际命中目标时才扣除消耗（适合弹药类消耗） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Costs)
	bool bOnlyApplyCostOnHit = false;
};
