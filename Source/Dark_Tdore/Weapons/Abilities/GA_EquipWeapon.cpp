// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/Abilities/GA_EquipWeapon.h"

#include "Equipment/Dark_TdoreEquipmentDefinition.h"
#include "Equipment/Dark_TdoreEquipmentInstance.h"
#include "Equipment/Dark_TdoreEquipmentManagerComponent.h"
#include "Weapons/Dark_TdoreWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GA_EquipWeapon)

UGA_EquipWeapon::UGA_EquipWeapon(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 武器切换是一次性按键行为，不需要持续激活。
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;
	// 同一时间只允许一个切武器技能运行，避免 2/3/4 连按时装备列表被并发修改。
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Exclusive_Replaceable;
	// 装备列表由服务器权威修改；客户端按键会请求服务器执行这个技能。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UGA_EquipWeapon::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	if (!EquipmentDefinition || !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}

	// 只有拥有 EquipmentManager 的 Pawn 才能使用此技能。
	return ActorInfo->AvatarActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>() != nullptr;
}

void UGA_EquipWeapon::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UDark_TdoreEquipmentManagerComponent* EquipmentManager = AvatarActor ? AvatarActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>() : nullptr;
	if (!EquipmentManager || !EquipmentDefinition)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 如果目标武器已经装备，按配置决定是保持装备状态，还是作为 Toggle 卸下。
	if (UDark_TdoreEquipmentInstance* ExistingInstance = EquipmentManager->GetFirstInstanceOfDefinition(EquipmentDefinition))
	{
		if (bToggleOffIfAlreadyEquipped)
		{
			EquipmentManager->UnequipItem(ExistingInstance);
		}

		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (bUnequipOtherWeapons)
	{
		// 当前阶段先做 Lyra QuickBar 的简化版：主武器槽只保留一把 WeaponInstance。
		EquipmentManager->UnequipAllItemsOfType(UDark_TdoreWeaponInstance::StaticClass());
	}

	EquipmentManager->EquipItem(EquipmentDefinition);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
