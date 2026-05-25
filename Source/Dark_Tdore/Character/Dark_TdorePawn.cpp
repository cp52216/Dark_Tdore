// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdorePawn.h"

#include "Dark_TdoreHealthComponent.h"
#include "Dark_TdorePawnExtensionComponent.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/Dark_TdoreHealthSet.h"
#include "AbilitySystem/Attributes/Dark_TdoreCombatSet.h"
#include "AbilitySystem/Dark_TdoreAbilitySet.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Components/GameFrameworkComponentManager.h"

ADark_TdorePawn::ADark_TdorePawn()
{
	PawnExtComponent = CreateDefaultSubobject<UDark_TdorePawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	PawnExtComponent->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::SetupAbilitySystem));

	HealthComponent = CreateDefaultSubobject<UDark_TdoreHealthComponent>(TEXT("HealthComponent"));
}

UAbilitySystemComponent* ADark_TdorePawn::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ADark_TdorePawn::BeginPlay()
{
	Super::BeginPlay();
}

// 参考 Lyra ALyraPawn：被 Controller 接管时通知 PawnExtComp
// PawnExtComp 检查 InitState 条件（有 Controller 后可从 Spawned → DataAvailable）
void ADark_TdorePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	PawnExtComponent->HandleControllerChanged();
}

// 解除接管时通知 PawnExtComp 更新 ASC Owner 关联
void ADark_TdorePawn::UnPossessed()
{
	Super::UnPossessed();
	PawnExtComponent->HandleControllerChanged();
}

// 网络复制 PlayerState 后通知 PawnExtComp 推进 InitState
void ADark_TdorePawn::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	PawnExtComponent->HandlePlayerStateReplicated();
}

// 参考 Lyra ALyraPawn：PawnExtComp 的 ASC 初始化回调
// ASC 由构造函数创建、由子类在构造中创建、或在此运行时创建（延迟初始化）
// 1. InitializeAbilitySystem — 将 Pawn 设为 ASC 的 Avatar Actor
// 2. AbilitySet::GiveToAbilitySystem — 通过 DataAsset 授予 GA/GE
void ADark_TdorePawn::SetupAbilitySystem()
{
	if (!PawnExtComponent) return;

	// 如果子类未在构造中创建 ASC，在此延迟创建（适用于蓝图子类）
	// 参考 Lyra ALyraCharacterWithAbilities：同时创建 HealthSet 和 CombatSet
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = NewObject<UDark_TdoreAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
		AbilitySystemComponent->SetIsReplicated(true);
		AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
		AbilitySystemComponent->RegisterComponent();

		HealthSet = NewObject<UDark_TdoreHealthSet>(this, TEXT("HealthSet"));
		CombatSet = NewObject<UDark_TdoreCombatSet>(this, TEXT("CombatSet"));
	}

	PawnExtComponent->InitializeAbilitySystem(Cast<UDark_TdoreAbilitySystemComponent>(AbilitySystemComponent), this);

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("Pawn ASC 已为 %s 初始化"), *GetName());

	HealthComponent->InitializeWithAbilitySystem(Cast<UDark_TdoreAbilitySystemComponent>(AbilitySystemComponent));

	if (AbilitySet)
	{
		AbilitySet->GiveToAbilitySystem(Cast<UDark_TdoreAbilitySystemComponent>(AbilitySystemComponent), this);
	}
}
