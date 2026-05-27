// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dark_TdoreTeamInfoBase.h"
#include "Dark_TdoreTeamPublicInfo.generated.h"

class UDark_TdoreTeamCreationComponent;
class UDark_TdoreTeamDisplayAsset;

/**
 * ADark_TdoreTeamPublicInfo — 阵营公开信息 Actor（参考 Lyra ALyraTeamPublicInfo）
 *
 * 继承 ADark_TdoreTeamInfoBase，额外存储阵营显示资产（TeamDisplayAsset）。
 *
 * 由 TeamCreationComponent::CreateTeam 创建并设置：
 *   1. SpawnActor 生成 Actor
 *   2. SetTeamId 设置阵营 ID
 *   3. SetTeamDisplayAsset 设置显示资产
 *   4. 后续注册到 TeamSubsystem
 *
 * 此 Actor 对所有客户端可见（bAlwaysRelevant=true 继承自基类）。
 */
UCLASS()
class ADark_TdoreTeamPublicInfo : public ADark_TdoreTeamInfoBase
{
	GENERATED_BODY()

	friend UDark_TdoreTeamCreationComponent;

public:
	ADark_TdoreTeamPublicInfo(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 获取阵营显示资产（颜色/纹理/名称） */
	UDark_TdoreTeamDisplayAsset* GetTeamDisplayAsset() const { return TeamDisplayAsset; }

private:
	// ===== 网络复制回调 =====
	UFUNCTION()
	void OnRep_TeamDisplayAsset();

	/** 设置显示资产（仅服务器，由 TeamCreationComponent 通过 friend 调用） */
	void SetTeamDisplayAsset(TObjectPtr<UDark_TdoreTeamDisplayAsset> NewDisplayAsset);

	// ===== 阵营显示资产 =====

	/** 阵营显示资产（颜色、纹理、名称等 — 数据驱动的视觉配置） */
	UPROPERTY(ReplicatedUsing = OnRep_TeamDisplayAsset)
	TObjectPtr<UDark_TdoreTeamDisplayAsset> TeamDisplayAsset;
};
