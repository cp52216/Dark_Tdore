// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Dark_TdoreGameplayAbility.h"
#include "GA_EquipWeapon.generated.h"

class UDark_TdoreEquipmentDefinition;
class UDark_TdoreEquipmentInstance;

/**
 * 数据驱动的武器装备 GameplayAbility。
 *
 * 命名和放置方式参照项目已有的 GA_Death / GA_TestQ：
 * - 文件路径：Weapons/Abilities/GA_EquipWeapon
 * - C++ 类名：UGA_EquipWeapon
 *
 * 用法：
 * 1. 创建蓝图类 GA_EquipWeapon_Sword，父类选 GA_EquipWeapon。
 * 2. 在蓝图默认值里把 EquipmentDefinition 填成 Sword 装备定义。
 * 3. 在 PawnData 的 AbilitySet 里授予 GA_EquipWeapon_Sword，并把 InputTag 配成 InputTag.Ability.Weapon.2。
 *
 * 后续 3/4/5 号武器只需要复制一个 GA 蓝图子类，换 EquipmentDefinition 和 InputTag，
 * C++ 不需要再写“按几装备什么”的硬编码。
 */
UCLASS(Abstract, Blueprintable)
class UGA_EquipWeapon : public UDark_TdoreGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EquipWeapon(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	/** 这个技能要装备的武器定义。例如 GA_EquipWeapon_Sword 填蓝图类 B_EquipmentDefinition_Sword。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition;

	/** 装备新武器前，是否卸下当前所有 WeaponInstance。主武器槽默认只允许一把武器。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	bool bUnequipOtherWeapons = true;

	/** 再按一次同一个武器键时是否卸下。默认 true：按 2 拿出 Sword，再按 2 收回并回到初始 ABP。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	bool bToggleOffIfAlreadyEquipped = true;
};
