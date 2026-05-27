// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameStateComponent.h"
#include "Dark_TdoreTeamCreationComponent.generated.h"

class ADark_TdoreTeamPublicInfo;
class UDark_TdoreTeamDisplayAsset;

/**
 * UDark_TdoreTeamCreationComponent — 阵营创建组件（参考 Lyra ULyraTeamCreationComponent）
 *
 * 挂载在 GameState 上（作为 GameStateComponent），在 BeginPlay 时：
 *   1. 根据 TeamsToCreate 配置创建阵营 Actor（PublicInfo）
 *   2. 分配已有玩家到人数最少的阵营
 *
 * 配置方式（蓝图）：
 *   在 GameState 蓝图 → Components → 添加 Dark_TdoreTeamCreationComponent
 *   → TeamsToCreate:
 *       [0] TeamId=0, DisplayAsset=DA_TeamRed
 *       [1] TeamId=1, DisplayAsset=DA_TeamBlue
 *   → PublicTeamInfoClass: BP_TeamPublicInfo（或默认 C++ 类）
 */
UCLASS(Blueprintable)
class UDark_TdoreTeamCreationComponent : public UGameStateComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreTeamCreationComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ===== UActorComponent 生命周期 =====
	virtual void BeginPlay() override;  // 自动创建阵营 + 分配玩家

protected:
	// ===== 蓝图可调用 =====

	/**
	 * 创建一个阵营
	 * @param TeamId       阵营 ID
	 * @param DisplayAsset 显示资产（可为 nullptr — 将不设置阵营视觉）
	 */
	UFUNCTION(BlueprintCallable, Category = Teams)
	void CreateTeam(int32 TeamId, UDark_TdoreTeamDisplayAsset* DisplayAsset);

protected:
	// ===== 配置属性（蓝图可编辑） =====

	/** 要创建的阵营列表（TeamId → DisplayAsset） */
	UPROPERTY(EditDefaultsOnly, Category = Teams)
	TMap<uint8, TObjectPtr<UDark_TdoreTeamDisplayAsset>> TeamsToCreate;

	/** PublicInfo 的蓝图子类（默认 C++ 类 ADark_TdoreTeamPublicInfo） */
	UPROPERTY(EditDefaultsOnly, Category = Teams)
	TSubclassOf<ADark_TdoreTeamPublicInfo> PublicTeamInfoClass;
};
