// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/Dark_TdoreGameplayAbility_MeleeCombo.h"

#include "Combat/Dark_TdoreCombatInputBufferComponent.h"
#include "Weapons/Dark_TdoreWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGameplayAbility_MeleeCombo)

UDark_TdoreGameplayAbility_MeleeCombo::UDark_TdoreGameplayAbility_MeleeCombo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Exclusive_Replaceable;
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UDark_TdoreWeaponInstance* UDark_TdoreGameplayAbility_MeleeCombo::GetWeaponInstance() const
{
	return Cast<UDark_TdoreWeaponInstance>(GetAssociatedEquipment());
}

UDark_TdoreCombatInputBufferComponent* UDark_TdoreGameplayAbility_MeleeCombo::GetInputBufferComponent() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	return AvatarActor ? AvatarActor->FindComponentByClass<UDark_TdoreCombatInputBufferComponent>() : nullptr;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::TryConsumeNextComboInput(FGameplayTagContainer AllowedInputTags, FGameplayTag& OutInputTag)
{
	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = GetInputBufferComponent())
	{
		return BufferComponent->TryConsumeBufferedInput(AllowedInputTags, OutInputTag);
	}

	OutInputTag = FGameplayTag();
	return false;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::FindComboStepForInput(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const
{
	return ComboData ? ComboData->FindStepForInputTag(InputTag, OutStep) : false;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::TryStartNextComboStepFromBuffer(const FGameplayTagContainer& AllowedInputTags, FDark_TdoreComboStep& OutStep)
{
	OutStep = FDark_TdoreComboStep();

	// 这里把“消费输入 -> 查数据 -> 启动下一段”合成一个函数，
	// 蓝图只需要在动画预输入窗口里调用它，不需要重复写这段样板逻辑。
	FGameplayTag ConsumedInputTag;
	if (!TryConsumeNextComboInput(AllowedInputTags, ConsumedInputTag))
	{
		return false;
	}

	if (!FindComboStepForInput(ConsumedInputTag, OutStep))
	{
		return false;
	}

	K2_StartComboStep(OutStep);
	return true;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return GetAssociatedEquipment() != nullptr;
}

void UDark_TdoreGameplayAbility_MeleeCombo::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FDark_TdoreComboStep Step;
	const bool bFoundDefaultStep = ComboData && ComboData->FindStepByName(DefaultStepName, Step);
	if (!bFoundDefaultStep)
	{
		// 配置不完整时立刻结束技能，避免 Ability 已激活但没有动画/结束点，导致输入被长期占用。
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	K2_StartComboStep(Step);
}
