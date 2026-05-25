// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dark_TdoreCameraMode.h"
#include "Dark_TdorePenetrationAvoidanceFeeler.h"
#include "Dark_TdoreCameraMode_ThirdPerson.generated.h"

class UCurveVector;

/**
 * UDark_TdoreCameraMode_ThirdPerson — 第三人称摄像机模式（参考 Lyra ULyraCameraMode_ThirdPerson）
 *
 * 在角色后方偏移的第三人称视角，支持：
 *   - Pitch 相关的偏移曲线（或默认固定偏移）
 *   - 蹲伏时自动下移 + 平滑过渡
 *   - 穿透避免：7 条扇形射线检测障碍物，自动拉近 + 平滑恢复
 *
 * 偏移计算：
 *   Pivot = GetPivotLocation()     ← 角色眼睛/胶囊顶
 *   偏移  = Curve(Pitch) 或 DefaultCameraOffset
 *   View.Location = Pivot + PivotRotation.RotateVector(偏移)
 *
 * 穿透避免流程（每帧）：
 *   1. 从 SafeLoc → CameraLoc 发射 7 条扇形 Sweep 射线
 *   2. 主射线命中 → 硬碰撞百分比（立即生效）
 *   3. 侧/上/下射线命中 → 软碰撞百分比（PenetrationBlendInTime 平滑拉入）
 *   4. DistBlockedPct = FMath::Min(硬, 软)，平滑变化
 *   5. CameraLoc = SafeLoc + (CameraLoc - SafeLoc) * DistBlockedPct
 *
 * 蓝图配置建议：
 *   - DefaultCameraOffset (-400, 0, 50)：后方 4 米，上方 50cm
 *   - FieldOfView 90：更宽的视角
 *   - BlendTime 0：作为默认模式不需要混合时间
 */
UCLASS(Abstract, Blueprintable)
class UDark_TdoreCameraMode_ThirdPerson : public UDark_TdoreCameraMode
{
	GENERATED_BODY()

public:
	UDark_TdoreCameraMode_ThirdPerson();

protected:
	//~UDark_TdoreCameraMode 接口
	virtual void UpdateView(float DeltaTime) override;

	/** 检测目标蹲伏状态，设置蹲伏偏移 */
	void UpdateForTarget(float DeltaTime);

	/** 运行穿透避免检测（7 条扇形射线） */
	void UpdatePreventPenetration(float DeltaTime);

	/**
	 * 穿透避免主逻辑。
	 * @param ViewTarget   忽略碰撞的目标 Actor
	 * @param SafeLoc      安全位置（角色胶囊体附近）
	 * @param CameraLoc    期望的摄像机位置（输入/输出）
	 * @param DistBlockedPct 被遮挡的百分比（0=完全遮挡，1=无遮挡）
	 * @param bSingleRayOnly 仅用主射线（禁用预测性检测）
	 */
	void PreventCameraPenetration(AActor const& ViewTarget, FVector const& SafeLoc, FVector& CameraLoc,
		float const& DeltaTime, float& DistBlockedPct, bool bSingleRayOnly);

	/** 设置蹲伏目标偏移，重置混合进度 */
	void SetTargetCrouchOffset(FVector NewTargetOffset);

	/** 每帧 EaseInOut 混合蹲伏偏移 */
	void UpdateCrouchOffset(float DeltaTime);

protected:
	// ========== 偏移 ==========

	/**
	 * 基于 Pitch 的偏移曲线（UCurveVector 资产）。
	 * X=后方距离，Y=侧向偏移，Z=高度偏移。
	 * 设置后 DefaultCameraOffset 不再生效。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Third Person")
	TObjectPtr<const UCurveVector> TargetOffsetCurve;

	/**
	 * 默认固定偏移（无 TargetOffsetCurve 且 bUseRuntimeFloatCurves=false 时生效）。
	 * (-400, 0, 50) = 角色后方 4 米，上方 50cm
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Third Person")
	FVector DefaultCameraOffset = FVector(-400.0f, 0.0f, 50.0f);

	/** 使用 FRuntimeFloatCurve 替代 UCurveVector 资产（可在蓝图中直接编辑曲线） */
	UPROPERTY(EditDefaultsOnly, Category = "Third Person")
	bool bUseRuntimeFloatCurves = false;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetX;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetY;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetZ;

	// ========== 蹲伏 ==========

	/**
	 * 蹲伏偏移混合速度乘数。
	 * 5.0 表示每秒 5 倍速混合（标准蹲伏约 0.2 秒完成过渡）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person")
	float CrouchOffsetBlendMultiplier = 5.0f;

	// ========== 穿透避免 ==========

	/** 被遮挡时拉近的混合时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float PenetrationBlendInTime = 0.1f;

	/** 遮挡消失时推远的混合时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float PenetrationBlendOutTime = 0.15f;

	/** 是否启用穿透检测（禁用时相机可穿墙） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bPreventPenetration = true;

	/**
	 * 是否启用预测性检测。
	 * true: 除主射线外还检测侧方/上方，提前预判旋转导致的遮挡
	 * false: 仅主射线，性能更好但可能有"弹出"现象
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bDoPredictiveAvoidance = true;

	/** 碰撞后从命中点推回的距离（避免摄像机嵌入碰撞体） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float CollisionPushOutDistance = 2.f;

	/**
	 * 当遮挡百分比低于此值时触发 OnCameraPenetratingTarget（百分比 0~1）。
	 * 可用于在相机被严重遮挡时隐藏角色或调整透明度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float ReportPenetrationPercent = 0.f;

	/** 防穿透触角射线数组（默认 7 条：中心 + 左右 ±16°/±32° + 上下 ±20°） */
	UPROPERTY(EditDefaultsOnly, Category = "Collision")
	TArray<FDark_TdorePenetrationAvoidanceFeeler> PenetrationAvoidanceFeelers;

	// ========== 运行时状态 ==========

	/** 主视线到期望位置的被遮挡百分比（运行时 transient） */
	UPROPERTY(Transient)
	float AimLineToDesiredPosBlockedPct = 1.f;

	/** 蹲伏偏移混合的起始值 */
	FVector InitialCrouchOffset = FVector::ZeroVector;

	/** 蹲伏偏移目标值 */
	FVector TargetCrouchOffset = FVector::ZeroVector;

	/** 蹲伏偏移混合进度（0→1） */
	float CrouchOffsetBlendPct = 1.0f;

	/** 当前蹲伏偏移值（运行时） */
	FVector CurrentCrouchOffset = FVector::ZeroVector;
};
