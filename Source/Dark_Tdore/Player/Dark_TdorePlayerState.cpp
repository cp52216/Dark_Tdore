// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdorePlayerState.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/Dark_TdoreHealthSet.h"
#include "AbilitySystem/Attributes/Dark_TdoreCombatSet.h"
#include "AbilitySystem/Dark_TdoreAbilitySet.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Character/Dark_TdorePawnData.h"

ADark_TdorePlayerState::ADark_TdorePlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UDark_TdoreAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UDark_TdoreHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UDark_TdoreCombatSet>(TEXT("CombatSet"));
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* ADark_TdorePlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ADark_TdorePlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	check(AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(this, GetPawn());

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("PlayerState ASC 已初始化: %s"), *GetNameSafe(this));
}

// 参考 Lyra ALyraPlayerState：通过 PawnData 的 AbilitySets 批量授予
void ADark_TdorePlayerState::BeginPlay()
{
	Super::BeginPlay();

	if (PawnData)
	{
		GrantAbilitySets();
	}
}

// 遍历 PawnData->AbilitySets，逐一授予 GA/GE
void ADark_TdorePlayerState::GrantAbilitySets()
{
	if (!AbilitySystemComponent) return;

	for (const UDark_TdoreAbilitySet* AbilitySet : PawnData->AbilitySets)
	{
		if (AbilitySet)
		{
			AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, nullptr);
		}
	}

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("PawnData 通过 %d 个 AbilitySet 授予技能"), PawnData->AbilitySets.Num());
}
