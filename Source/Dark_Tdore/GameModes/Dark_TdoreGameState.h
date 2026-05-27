// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModularGameState.h"
#include "Dark_TdoreGameState.generated.h"

class UDark_TdoreTeamCreationComponent;

/**
 * ADark_TdoreGameState — 游戏状态（参考 Lyra ALyraGameState）
 *
 * 包含：
 *   - TeamCreationComponent — 自动创建阵营并分配玩家
 *
 * 设置默认 GameState：Project Settings → Maps & Modes → GameState Class → 选此蓝图
 */
UCLASS()
class ADark_TdoreGameState : public AModularGameStateBase
{
	GENERATED_BODY()

public:
	ADark_TdoreGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	/** 阵营创建组件（自动创建配好的阵营） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Teams)
	TObjectPtr<UDark_TdoreTeamCreationComponent> TeamCreationComponent;
};
