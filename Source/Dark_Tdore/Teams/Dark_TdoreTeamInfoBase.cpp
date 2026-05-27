// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamInfoBase.h"
#include "Dark_TdoreTeamSubsystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamInfoBase)

// ============ 构造 ============

ADark_TdoreTeamInfoBase::ADark_TdoreTeamInfoBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TeamId(INDEX_NONE)
{
	// 网络配置：始终复制、始终相关（所有客户端都需要接收阵营信息）
	bReplicates = true;
	bAlwaysRelevant = true;
	NetPriority = 3.0f;
	SetReplicatingMovement(false);
}

// ============ 网络复制 ============

void ADark_TdoreTeamInfoBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// TeamTags — Full Replication（每次变化都复制）
	DOREPLIFETIME(ThisClass, TeamTags);

	// TeamId — InitialOnly（创建后不可变，只复制一次给后续加入的客户端）
	DOREPLIFETIME_CONDITION(ThisClass, TeamId, COND_InitialOnly);
}

// ============ 生命周期 ============

void ADark_TdoreTeamInfoBase::BeginPlay()
{
	Super::BeginPlay();

	// 阵营 Actor 生成后，自动注册到 TeamSubsystem
	TryRegisterWithTeamSubsystem();
}

void ADark_TdoreTeamInfoBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 阵营 Actor 销毁前，从 TeamSubsystem 注销
	if (TeamId != INDEX_NONE)
	{
		if (UDark_TdoreTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<UDark_TdoreTeamSubsystem>())
		{
			TeamSubsystem->UnregisterTeamInfo(this);
		}
		// 注意：EndPlay 时 Subsystem 可能已被销毁（如切换关卡），不检查确保安全
	}

	Super::EndPlay(EndPlayReason);
}

// ============ 注册到 TeamSubsystem ============

void ADark_TdoreTeamInfoBase::RegisterWithTeamSubsystem(UDark_TdoreTeamSubsystem* Subsystem)
{
	Subsystem->RegisterTeamInfo(this);
}

void ADark_TdoreTeamInfoBase::TryRegisterWithTeamSubsystem()
{
	if (TeamId != INDEX_NONE)
	{
		if (UDark_TdoreTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<UDark_TdoreTeamSubsystem>())
		{
			RegisterWithTeamSubsystem(TeamSubsystem);
		}
	}
}

// ============ TeamId 设置与复制 ============

void ADark_TdoreTeamInfoBase::SetTeamId(int32 NewTeamId)
{
	check(HasAuthority());             // 仅服务器可调用
	check(TeamId == INDEX_NONE);       // 只能设置一次
	check(NewTeamId != INDEX_NONE);    // 新 ID 必须有效

	TeamId = NewTeamId;

	// ID 设置后立即注册
	TryRegisterWithTeamSubsystem();
}

void ADark_TdoreTeamInfoBase::OnRep_TeamId()
{
	// 客户端收到 TeamId 复制后注册
	TryRegisterWithTeamSubsystem();
}
