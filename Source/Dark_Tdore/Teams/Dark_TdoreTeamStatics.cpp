// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamStatics.h"
#include "Dark_TdoreTeamSubsystem.h"
#include "Dark_TdoreTeamDisplayAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamStatics)

// ============ FindTeamFromObject ============
// 蓝图工具函数：查询对象阵营信息，一步返回所有相关数据
//
// 内部实现：
//   1. 通过 GEngine 获取 World
//   2. 从 World 获取 TeamSubsystem
//   3. 调用 FindTeamFromObject 查阵营 ID
//   4. 如果找到了，额外获取显示资产

void UDark_TdoreTeamStatics::FindTeamFromObject(const UObject* Agent, bool& bIsPartOfTeam,
	int32& TeamId, UDark_TdoreTeamDisplayAsset*& DisplayAsset)
{
	// 默认值：无阵营
	bIsPartOfTeam = false;
	TeamId = INDEX_NONE;
	DisplayAsset = nullptr;

	// Step 1: 获取 World
	if (UWorld* World = GEngine->GetWorldFromContextObject(Agent, EGetWorldErrorMode::LogAndReturnNull))
	{
		// Step 2: 获取 TeamSubsystem
		if (UDark_TdoreTeamSubsystem* TeamSubsystem = World->GetSubsystem<UDark_TdoreTeamSubsystem>())
		{
			// Step 3: 查找阵营 ID
			TeamId = TeamSubsystem->FindTeamFromObject(Agent);
			if (TeamId != INDEX_NONE)
			{
				// 找到阵营 → 获取显示资产
				bIsPartOfTeam = true;
				DisplayAsset = TeamSubsystem->GetTeamDisplayAsset(TeamId, INDEX_NONE);
			}
		}
	}
}
