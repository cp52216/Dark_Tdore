// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGamePhaseAbility.h"
#include "AbilitySystemComponent.h"
#include "Dark_TdoreGamePhaseSubsystem.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGamePhaseAbility)

UDark_TdoreGamePhaseAbility::UDark_TdoreGamePhaseAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 阶段能力：不复制、每实例独立、仅服务器执行
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}

// 激活时：通知 PhaseSubsystem→ OnBeginPhase（取消冲突的旧阶段）
void UDark_TdoreGamePhaseAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (ActorInfo->IsNetAuthority())
	{
		UWorld* World = ActorInfo->AbilitySystemComponent->GetWorld();
		UDark_TdoreGamePhaseSubsystem* PhaseSubsystem = UWorld::GetSubsystem<UDark_TdoreGamePhaseSubsystem>(World);
		PhaseSubsystem->OnBeginPhase(this, Handle);
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

// 结束时：通知 PhaseSubsystem→ OnEndPhase（回调 PhaseEnded，通知观察者）
void UDark_TdoreGamePhaseAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo->IsNetAuthority())
	{
		UWorld* World = ActorInfo->AbilitySystemComponent->GetWorld();
		UDark_TdoreGamePhaseSubsystem* PhaseSubsystem = UWorld::GetSubsystem<UDark_TdoreGamePhaseSubsystem>(World);
		PhaseSubsystem->OnEndPhase(this, Handle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
