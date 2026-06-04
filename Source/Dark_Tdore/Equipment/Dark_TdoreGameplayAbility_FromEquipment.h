// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Dark_TdoreGameplayAbility.h"
#include "Dark_TdoreGameplayAbility_FromEquipment.generated.h"

class UDark_TdoreEquipmentInstance;

/**
 * 从装备授予的 GameplayAbility 基类，参考 LyraGameplayAbility_FromEquipment。
 *
 * EquipmentDefinition.AbilitySetsToGrant 授予技能时，会把 SourceObject 设置为装备实例。
 * 继承这个类的武器技能可以通过 GetAssociatedEquipment() 反查“是哪一件装备授予了我”，
 * 后续做剑攻击、弓箭、法器时就能从对应 WeaponInstance 上读取冷却、动画层、伤害配置等数据。
 */
UCLASS(Abstract)
class UDark_TdoreGameplayAbility_FromEquipment : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UDark_TdoreGameplayAbility_FromEquipment(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 获取授予此技能的装备实例。只有由 EquipmentDefinition.AbilitySetsToGrant 授予的技能才会返回有效值。 */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Ability")
	UDark_TdoreEquipmentInstance* GetAssociatedEquipment() const;
};
