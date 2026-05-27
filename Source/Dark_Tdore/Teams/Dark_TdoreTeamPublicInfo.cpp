// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamPublicInfo.h"
#include "Net/UnrealNetwork.h"
#include "Dark_TdoreTeamInfoBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamPublicInfo)

// ============ 构造 ============

ADark_TdoreTeamPublicInfo::ADark_TdoreTeamPublicInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ============ 网络复制 ============

void ADark_TdoreTeamPublicInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// TeamDisplayAsset — InitialOnly（创建后不可变）
	DOREPLIFETIME_CONDITION(ThisClass, TeamDisplayAsset, COND_InitialOnly);
}

// ============ Setter / 复制回调 ============

void ADark_TdoreTeamPublicInfo::SetTeamDisplayAsset(TObjectPtr<UDark_TdoreTeamDisplayAsset> NewDisplayAsset)
{
	check(HasAuthority());

	TeamDisplayAsset = NewDisplayAsset;

	// 设置后重新注册到 TeamSubsystem（更新其缓存的 DisplayAsset）
	TryRegisterWithTeamSubsystem();
}

void ADark_TdoreTeamPublicInfo::OnRep_TeamDisplayAsset()
{
	// 客户端收到显示资产复制后，重新注册以更新缓存
	TryRegisterWithTeamSubsystem();
}
