// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Info.h"
#include "System/GameplayTagStack.h"
#include "Dark_TdoreTeamInfoBase.generated.h"

class UDark_TdoreTeamCreationComponent;
class UDark_TdoreTeamSubsystem;

/**
 * ADark_TdoreTeamInfoBase — 阵营信息 Actor 基类（参考 Lyra ALyraTeamInfoBase）
 *
 * 每个阵营在 World 中有一个 Actor 实例，由 TeamCreationComponent 创建。
 *
 * 职责：
 *   - 存储阵营 ID（网络复制到客户端）
 *   - 持有阵营级别标签（通过 FGameplayTagStackContainer TeamTags）
 *     → 如 "Team.HasCapturePoint", "Team.IsWinning"
 *   - BeginPlay 时自动注册到 TeamSubsystem
 *   - EndPlay 时自动从 TeamSubsystem 注销
 *
 * 子类：
 *   ADark_TdoreTeamPublicInfo  — 含显示资产，所有客户端可见
 *   （PrivateInfo 可后续按需添加）
 */
UCLASS(Abstract)
class ADark_TdoreTeamInfoBase : public AInfo
{
	GENERATED_BODY()

public:
	ADark_TdoreTeamInfoBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 获取阵营 ID */
	int32 GetTeamId() const { return TeamId; }

	// ===== AActor 生命周期 =====
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** 子类覆写注册逻辑 */
	virtual void RegisterWithTeamSubsystem(UDark_TdoreTeamSubsystem* Subsystem);

	/** 尝试注册（仅在 TeamId != INDEX_NONE 时执行） */
	void TryRegisterWithTeamSubsystem();

private:
	/** 设置阵营 ID（仅服务器，仅可调用一次） */
	void SetTeamId(int32 NewTeamId);

	/** 网络复制回调：客户端收到 TeamId 后自动注册 */
	UFUNCTION()
	void OnRep_TeamId();

public:
	/** CloudCreationComponent 通过 SetTeamId 设置阵营 */
	friend UDark_TdoreTeamCreationComponent;

	// ===== 阵营标签（GameplayTagStack 堆叠系统）=====

	/**
	 * 阵营级别的 GameplayTag 堆叠容器
	 *
	 * 用途：给阵营附加标签，用于游戏逻辑判断
	 * 示例：
	 *   TeamTags.AddStack("Status.HasFlag", 1);        // 阵营持有旗帜
	 *   TeamTags.AddStack("Score.Multiplier", 2);      // 阵营分数倍率 ×2
	 *   int32 Multiplier = TeamTags.GetStackCount(Tag); // 查询倍率
	 *
	 * 此属性会通过网络复制到所有客户端。
	 */
	UPROPERTY(Replicated)
	FGameplayTagStackContainer TeamTags;

private:
	/** 阵营 ID（网络复制到客户端，InitialOnly — 创建后不可变） */
	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = INDEX_NONE;
};
