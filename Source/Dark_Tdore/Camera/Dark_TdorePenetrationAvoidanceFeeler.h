// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dark_TdorePenetrationAvoidanceFeeler.generated.h"

/**
 * FDark_TdorePenetrationAvoidanceFeeler — 防穿透触角射线（参考 Lyra）
 *
 * 第三人称摄像机在角色背后时可能被墙壁遮挡。
 * 使用多条扇形射线检测障碍物，将摄像机平滑拉近以避免穿墙。
 *
 * 成员说明：
 *   AdjustmentRot  — 相对于主射线的偏转角度（Pitch/Yaw）
 *   WorldWeight    — 命中世界几何体时的影响权重（0~1）
 *   PawnWeight     — 命中其他 Pawn 时的影响权重（0=忽略 Pawn）
 *   Extent         — 射线球形扫描半径
 *   TraceInterval  — 两次射线检测之间的帧间隔（优化性能）
 *   FramesUntilNextTrace — 下次检测前的剩余帧数（运行时，transient）
 *
 * 默认 7 条射线配置（参考 Lyra）：
 *   索引 0: 中心主射线，权重 1.0，每帧检测
 *   索引 1: 右偏 16°，权重 0.75，每 3 帧检测
 *   索引 2: 左偏 16°，权重 0.75
 *   索引 3: 右偏 32°，权重 0.50，每 5 帧检测
 *   索引 4: 左偏 32°，权重 0.50
 *   索引 5: 上偏 20°，权重 1.0，每 4 帧检测
 *   索引 6: 下偏 20°，权重 0.50
 */
USTRUCT()
struct FDark_TdorePenetrationAvoidanceFeeler
{
	GENERATED_BODY()

	/** 相对于主射线的偏转角度 */
	UPROPERTY(EditAnywhere, Category = PenetrationAvoidanceFeeler)
	FRotator AdjustmentRot;

	/** 命中世界几何体时的影响权重（0~1） */
	UPROPERTY(EditAnywhere, Category = PenetrationAvoidanceFeeler)
	float WorldWeight = 0.f;

	/** 命中 Pawn 时的影响权重（0 = 忽略 Pawn 碰撞） */
	UPROPERTY(EditAnywhere, Category = PenetrationAvoidanceFeeler)
	float PawnWeight = 0.f;

	/** 射线球形扫描半径 */
	UPROPERTY(EditAnywhere, Category = PenetrationAvoidanceFeeler)
	float Extent = 0.f;

	/** 两条射线之间的最小帧间隔（优化性能） */
	UPROPERTY(EditAnywhere, Category = PenetrationAvoidanceFeeler)
	int32 TraceInterval = 0;

	/** 下次检测前的剩余帧数（运行时 transient，自动倒数） */
	UPROPERTY(transient)
	int32 FramesUntilNextTrace = 0;

	FDark_TdorePenetrationAvoidanceFeeler()
		: AdjustmentRot(ForceInit)
	{
	}

	FDark_TdorePenetrationAvoidanceFeeler(
		const FRotator& InAdjustmentRot, float InWorldWeight, float InPawnWeight,
		float InExtent, int32 InTraceInterval = 0, int32 InFramesUntilNextTrace = 0)
		: AdjustmentRot(InAdjustmentRot)
		, WorldWeight(InWorldWeight)
		, PawnWeight(InPawnWeight)
		, Extent(InExtent)
		, TraceInterval(InTraceInterval)
		, FramesUntilNextTrace(InFramesUntilNextTrace)
	{
	}
};
