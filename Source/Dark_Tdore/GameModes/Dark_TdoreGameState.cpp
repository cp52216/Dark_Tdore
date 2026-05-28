// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGameState.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Phases/Dark_TdoreGamePhaseAbility.h"
#include "AbilitySystem/Phases/Dark_TdoreGamePhaseSubsystem.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Teams/Dark_TdoreTeamCreationComponent.h"
#include "Engine/World.h"

ADark_TdoreGameState::ADark_TdoreGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UDark_TdoreAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	TeamCreationComponent = CreateDefaultSubobject<UDark_TdoreTeamCreationComponent>(TEXT("TeamCreationComponent"));
}

UAbilitySystemComponent* ADark_TdoreGameState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

// ASC 初始化：Owner 和 Avatar 都是 GameState 自身（参考 Lyra）
void ADark_TdoreGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	check(AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(/*Owner=*/ this, /*Avatar=*/ this);
}

// BeginPlay → 如果配置了 InitialPhaseClass → 自动启动初始阶段
void ADark_TdoreGameState::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogDark_TdoreGAS, Display, TEXT("[GameState::BeginPlay] HasAuthority=%d, InitialPhaseClass=%s, PhaseSubsystem=%s"),
		HasAuthority(),
		*GetNameSafe(InitialPhaseClass),
		*GetNameSafe(GetWorld()->GetSubsystem<UDark_TdoreGamePhaseSubsystem>()));

	if (HasAuthority() && InitialPhaseClass)
	{
		UDark_TdoreGamePhaseSubsystem* PhaseSubsystem =
			GetWorld()->GetSubsystem<UDark_TdoreGamePhaseSubsystem>();
		if (PhaseSubsystem)
		{
			UE_LOG(LogDark_TdoreGAS, Display, TEXT("[GameState::BeginPlay] 启动阶段: %s"), *GetNameSafe(InitialPhaseClass));
			PhaseSubsystem->StartPhase(InitialPhaseClass);
		}
	}
	else
	{
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[GameState::BeginPlay] 阶段未启动: HasAuthority=%d, InitialPhaseClass=%s"),
			HasAuthority(), *GetNameSafe(InitialPhaseClass));
	}
}
