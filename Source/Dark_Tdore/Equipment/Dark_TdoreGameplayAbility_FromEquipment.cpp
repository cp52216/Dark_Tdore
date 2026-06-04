// Copyright Epic Games, Inc. All Rights Reserved.

#include "Equipment/Dark_TdoreGameplayAbility_FromEquipment.h"

#include "Equipment/Dark_TdoreEquipmentInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGameplayAbility_FromEquipment)

UDark_TdoreGameplayAbility_FromEquipment::UDark_TdoreGameplayAbility_FromEquipment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 装备授予的技能通常需要实例化，这样蓝图里保存的临时状态不会被所有角色共享。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UDark_TdoreEquipmentInstance* UDark_TdoreGameplayAbility_FromEquipment::GetAssociatedEquipment() const
{
	if (FGameplayAbilitySpec* Spec = UGameplayAbility::GetCurrentAbilitySpec())
	{
		return Cast<UDark_TdoreEquipmentInstance>(Spec->SourceObject.Get());
	}

	return nullptr;
}
