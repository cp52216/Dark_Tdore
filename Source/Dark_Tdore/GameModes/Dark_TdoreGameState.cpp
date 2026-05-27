// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGameState.h"
#include "Teams/Dark_TdoreTeamCreationComponent.h"

ADark_TdoreGameState::ADark_TdoreGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 创建阵营创建组件为默认子对象
	// 蓝图可覆写 TeamsToCreate 配置具体阵营
	TeamCreationComponent = CreateDefaultSubobject<UDark_TdoreTeamCreationComponent>(TEXT("TeamCreationComponent"));
}
