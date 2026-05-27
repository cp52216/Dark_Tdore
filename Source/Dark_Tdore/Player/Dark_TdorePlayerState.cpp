// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdorePlayerState.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/Dark_TdoreHealthSet.h"
#include "AbilitySystem/Attributes/Dark_TdoreCombatSet.h"
#include "AbilitySystem/Dark_TdoreAbilitySet.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Character/Dark_TdorePawnData.h"
#include "Net/UnrealNetwork.h"

ADark_TdorePlayerState::ADark_TdorePlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UDark_TdoreAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UDark_TdoreHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UDark_TdoreCombatSet>(TEXT("CombatSet"));
	SetNetUpdateFrequency(100.0f);

	MyTeamID = FGenericTeamId::NoTeam;
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

// ============ 阵营系统（IDark_TdoreTeamAgentInterface 实现）============
// PlayerState 是阵营归属的权威来源 — 阵营切换仅通过 SetGenericTeamId
// 阵营变更通过 ConditionalBroadcastTeamChanged 通知 TeamSubsystem 和其他监听者

// 网络复制声明（MyTeamID 需要复制到客户端）
void ADark_TdorePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MyTeamID);
}

// SetGenericTeamId — 设置阵营（仅服务器，通过 TeamCreationComponent 或 GM 命令调用）
// 流程：保存旧 ID → 设置新 ID → 条件广播（仅当变化时）
void ADark_TdorePlayerState::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (HasAuthority())
	{
		const FGenericTeamId OldTeamID = MyTeamID;
		MyTeamID = NewTeamID;

		// 仅当旧阵营 ≠ 新阵营时才触发委托广播
		ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID);
	}
}

// GetGenericTeamId — 供 TeamSubsystem::FindTeamFromObject 查询
FGenericTeamId ADark_TdorePlayerState::GetGenericTeamId() const
{
	return MyTeamID;
}

// GetOnTeamIndexChangedDelegate — 委托获取（由 ConditionalBroadcastTeamChanged 内部调用）
FOnDarkTdoreTeamIndexChangedDelegate* ADark_TdorePlayerState::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

// OnRep_MyTeamID — 客户端收到复制后触发（同样通过条件广播通知本地监听者）
void ADark_TdorePlayerState::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}
