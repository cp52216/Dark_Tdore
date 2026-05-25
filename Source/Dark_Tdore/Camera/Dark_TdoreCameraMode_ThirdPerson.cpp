// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCameraMode_ThirdPerson.h"
#include "Curves/CurveVector.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Math/RotationMatrix.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCameraMode_ThirdPerson)

// Actor 上带此 Tag 时，摄像机穿透检测忽略该 Actor
namespace DarkTdoreCameraMode_ThirdPerson
{
	static const FName NAME_IgnoreCameraCollision = TEXT("IgnoreCameraCollision");
}

// ============ 构造 — 初始化 7 条防穿透触角射线 ============

UDark_TdoreCameraMode_ThirdPerson::UDark_TdoreCameraMode_ThirdPerson()
{
	// 索引 0: 中心主射线，每帧检测，权重 1.0
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(+00.0f, +00.0f, 0.0f), 1.00f, 1.00f, 14.f, 0));

	// 索引 1-2: 左右 ±16°，权重 0.75，每 3 帧检测
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(+00.0f, +16.0f, 0.0f), 0.75f, 0.75f, 00.f, 3));
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(+00.0f, -16.0f, 0.0f), 0.75f, 0.75f, 00.f, 3));

	// 索引 3-4: 左右 ±32°，权重 0.50，每 5 帧检测
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(+00.0f, +32.0f, 0.0f), 0.50f, 0.50f, 00.f, 5));
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(+00.0f, -32.0f, 0.0f), 0.50f, 0.50f, 00.f, 5));

	// 索引 5-6: 上下 ±20°，权重 1.0/0.5
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(+20.0f, +00.0f, 0.0f), 1.00f, 1.00f, 00.f, 4));
	PenetrationAvoidanceFeelers.Add(FDark_TdorePenetrationAvoidanceFeeler(FRotator(-20.0f, +00.0f, 0.0f), 0.50f, 0.50f, 00.f, 4));
}

// ============ UpdateView — 计算第三人称视角 ============

void UDark_TdoreCameraMode_ThirdPerson::UpdateView(float DeltaTime)
{
	UpdateForTarget(DeltaTime);     // 蹲伏偏移
	UpdateCrouchOffset(DeltaTime);  // 平滑过渡蹲伏

	// Pivot = 角色眼睛位置 + 蹲伏偏移
	FVector PivotLocation = GetPivotLocation() + CurrentCrouchOffset;
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	// 设置基础视图（被偏移覆盖前）
	View.Location = PivotLocation;
	View.Rotation = PivotRotation;
	View.ControlRotation = View.Rotation;
	View.FieldOfView = FieldOfView;

	// 根据 Pitch 偏移摄像机位置（曲线驱动或固定默认偏移）
	if (!bUseRuntimeFloatCurves)
	{
		if (TargetOffsetCurve)
		{
			// 曲线资产：X=距离, Y=侧偏移, Z=高度
			const FVector TargetOffset = TargetOffsetCurve->GetVectorValue(PivotRotation.Pitch);
			View.Location = PivotLocation + PivotRotation.RotateVector(TargetOffset);
		}
		else
		{
			// 后备：DefaultCameraOffset（默认 -400, 0, 50）
			View.Location = PivotLocation + PivotRotation.RotateVector(DefaultCameraOffset);
		}
	}
	else
	{
		// 运行时浮动曲线（蓝图中直接编辑）
		FVector TargetOffset(0.0f);
		TargetOffset.X = TargetOffsetX.GetRichCurveConst()->Eval(PivotRotation.Pitch);
		TargetOffset.Y = TargetOffsetY.GetRichCurveConst()->Eval(PivotRotation.Pitch);
		TargetOffset.Z = TargetOffsetZ.GetRichCurveConst()->Eval(PivotRotation.Pitch);
		View.Location = PivotLocation + PivotRotation.RotateVector(TargetOffset);
	}

	// 穿透避免：检测障碍物并拉近
	UpdatePreventPenetration(DeltaTime);
}

// ============ 蹲伏偏移 ============

void UDark_TdoreCameraMode_ThirdPerson::UpdateForTarget(float DeltaTime)
{
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(GetTargetActor()))
	{
		if (TargetCharacter->IsCrouched())
		{
			// 蹲伏时眼睛高度下降，摄像机跟随下移
			const ACharacter* TargetCharacterCDO = TargetCharacter->GetClass()->GetDefaultObject<ACharacter>();
			const float CrouchedHeightAdjustment = TargetCharacterCDO->CrouchedEyeHeight - TargetCharacterCDO->BaseEyeHeight;
			SetTargetCrouchOffset(FVector(0.f, 0.f, CrouchedHeightAdjustment));
			return;
		}
	}
	// 未蹲伏 → 归零
	SetTargetCrouchOffset(FVector::ZeroVector);
}

void UDark_TdoreCameraMode_ThirdPerson::SetTargetCrouchOffset(FVector NewTargetOffset)
{
	// 重置混合进度，从当前偏移开始混合到目标
	CrouchOffsetBlendPct = 0.0f;
	InitialCrouchOffset = CurrentCrouchOffset;
	TargetCrouchOffset = NewTargetOffset;
}

void UDark_TdoreCameraMode_ThirdPerson::UpdateCrouchOffset(float DeltaTime)
{
	if (CrouchOffsetBlendPct < 1.0f)
	{
		// EaseInOut 平滑混合，速度由 CrouchOffsetBlendMultiplier 控制
		CrouchOffsetBlendPct = FMath::Min(CrouchOffsetBlendPct + DeltaTime * CrouchOffsetBlendMultiplier, 1.0f);
		CurrentCrouchOffset = FMath::InterpEaseInOut(InitialCrouchOffset, TargetCrouchOffset, CrouchOffsetBlendPct, 1.0f);
	}
	else
	{
		CurrentCrouchOffset = TargetCrouchOffset;
	}
}

// ============ 穿透避免 ============

void UDark_TdoreCameraMode_ThirdPerson::UpdatePreventPenetration(float DeltaTime)
{
	if (!bPreventPenetration) return;

	AActor* TargetActor = GetTargetActor();
	if (!TargetActor) return;

	APawn* TargetPawn = Cast<APawn>(TargetActor);

	const UPrimitiveComponent* PPActorRootComponent = Cast<UPrimitiveComponent>(TargetActor->GetRootComponent());
	if (PPActorRootComponent)
	{
		// 找到视线上最接近胶囊体中心的点
		FVector ClosestPointOnLineToCapsuleCenter;
		FVector SafeLocation = TargetActor->GetActorLocation();
		FMath::PointDistToLine(SafeLocation, View.Rotation.Vector(), View.Location, ClosestPointOnLineToCapsuleCenter);

		// 将安全位置限制在胶囊体高度范围内
		const float PushInDistance = PenetrationAvoidanceFeelers.Num() > 0
			? PenetrationAvoidanceFeelers[0].Extent + CollisionPushOutDistance
			: CollisionPushOutDistance;
		const float MaxHalfHeight = TargetActor->GetSimpleCollisionHalfHeight() - PushInDistance;
		SafeLocation.Z = FMath::Clamp(ClosestPointOnLineToCapsuleCenter.Z, SafeLocation.Z - MaxHalfHeight, SafeLocation.Z + MaxHalfHeight);

		// 推回胶囊体内（避免初始穿透）
		float DistanceSqr;
		PPActorRootComponent->GetSquaredDistanceToCollision(ClosestPointOnLineToCapsuleCenter, DistanceSqr, SafeLocation);
		if (PenetrationAvoidanceFeelers.Num() > 0)
		{
			SafeLocation += (SafeLocation - ClosestPointOnLineToCapsuleCenter).GetSafeNormal() * PushInDistance;
		}

		// 执行扇形射线检测
		const bool bSingleRayPenetrationCheck = !bDoPredictiveAvoidance;
		PreventCameraPenetration(*TargetActor, SafeLocation, View.Location, DeltaTime, AimLineToDesiredPosBlockedPct, bSingleRayPenetrationCheck);
	}
}

void UDark_TdoreCameraMode_ThirdPerson::PreventCameraPenetration(
	AActor const& ViewTarget, FVector const& SafeLoc, FVector& CameraLoc,
	float const& DeltaTime, float& DistBlockedPct, bool bSingleRayOnly)
{
	float HardBlockedPct = DistBlockedPct;  // 主射线命中率（立即生效）
	float SoftBlockedPct = DistBlockedPct;  // 侧射线命中率（平滑过渡）

	// 构建射线基准坐标系
	FVector BaseRay = CameraLoc - SafeLoc;
	FRotationMatrix BaseRayMatrix(BaseRay.Rotation());
	FVector BaseRayLocalUp, BaseRayLocalFwd, BaseRayLocalRight;
	BaseRayMatrix.GetScaledAxes(BaseRayLocalFwd, BaseRayLocalRight, BaseRayLocalUp);

	float DistBlockedPctThisFrame = 1.f;

	const int32 NumRaysToShoot = bSingleRayOnly ? FMath::Min(1, PenetrationAvoidanceFeelers.Num()) : PenetrationAvoidanceFeelers.Num();
	FCollisionQueryParams SphereParams(SCENE_QUERY_STAT(CameraPen), false);
	SphereParams.AddIgnoredActor(&ViewTarget);

	FCollisionShape SphereShape = FCollisionShape::MakeSphere(0.f);
	UWorld* World = GetWorld();

	for (int32 RayIdx = 0; RayIdx < NumRaysToShoot; ++RayIdx)
	{
		FDark_TdorePenetrationAvoidanceFeeler& Feeler = PenetrationAvoidanceFeelers[RayIdx];
		if (Feeler.FramesUntilNextTrace > 0)
		{
			--Feeler.FramesUntilNextTrace;
			continue; // 跳过本帧（性能优化：非主射线隔帧检测）
		}

		// 计算射线终点（带偏转角）
		FVector RayTarget;
		{
			FVector RotatedRay = BaseRay.RotateAngleAxis(Feeler.AdjustmentRot.Yaw, BaseRayLocalUp);
			RotatedRay = RotatedRay.RotateAngleAxis(Feeler.AdjustmentRot.Pitch, BaseRayLocalRight);
			RayTarget = SafeLoc + RotatedRay;
		}

		// 球形 Sweep 检测
		SphereShape.Sphere.Radius = Feeler.Extent;
		ECollisionChannel TraceChannel = ECC_Camera;

		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, SafeLoc, RayTarget, FQuat::Identity, TraceChannel, SphereShape, SphereParams);

		Feeler.FramesUntilNextTrace = Feeler.TraceInterval;

		if (bHit && Hit.GetActor())
		{
			// 带 IgnoreCameraCollision Tag 的 Actor 不参与遮挡
			if (Hit.GetActor()->ActorHasTag(DarkTdoreCameraMode_ThirdPerson::NAME_IgnoreCameraCollision))
			{
				SphereParams.AddIgnoredActor(Hit.GetActor());
				continue;
			}

			// 计算遮挡百分比（考虑命中物体权重）
			const float Weight = Cast<APawn>(Hit.GetActor()) ? Feeler.PawnWeight : Feeler.WorldWeight;
			float NewBlockPct = Hit.Time;
			NewBlockPct += (1.f - NewBlockPct) * (1.f - Weight);

			// 考虑推回距离重新计算
			NewBlockPct = ((Hit.Location - SafeLoc).Size() - CollisionPushOutDistance) / (RayTarget - SafeLoc).Size();
			DistBlockedPctThisFrame = FMath::Min(NewBlockPct, DistBlockedPctThisFrame);

			Feeler.FramesUntilNextTrace = 0; // 命中后下帧继续检测
		}

		if (RayIdx == 0)
		{
			HardBlockedPct = DistBlockedPctThisFrame;
		}
		else
		{
			SoftBlockedPct = DistBlockedPctThisFrame;
		}
	}

	// 平滑更新 DistBlockedPct
	if (bResetInterpolation)
	{
		DistBlockedPct = DistBlockedPctThisFrame;
	}
	else if (DistBlockedPct < DistBlockedPctThisFrame)
	{
		// 被遮挡 → 按 PenetrationBlendOutTime 速率推远
		if (PenetrationBlendOutTime > DeltaTime)
		{
			DistBlockedPct = DistBlockedPct + DeltaTime / PenetrationBlendOutTime * (DistBlockedPctThisFrame - DistBlockedPct);
		}
		else
		{
			DistBlockedPct = DistBlockedPctThisFrame;
		}
	}
	else
	{
		// 遮挡解除 → 按 PenetrationBlendInTime 速率拉近
		if (DistBlockedPct > HardBlockedPct)
		{
			DistBlockedPct = HardBlockedPct; // 硬遮挡立即生效
		}
		else if (DistBlockedPct > SoftBlockedPct)
		{
			if (PenetrationBlendInTime > DeltaTime)
			{
				DistBlockedPct = DistBlockedPct - DeltaTime / PenetrationBlendInTime * (DistBlockedPct - SoftBlockedPct);
			}
			else
			{
				DistBlockedPct = SoftBlockedPct;
			}
		}
	}

	// 应用最终遮挡百分比到摄像机位置
	DistBlockedPct = FMath::Clamp(DistBlockedPct, 0.f, 1.f);
	if (DistBlockedPct < (1.f - KINDA_SMALL_NUMBER))
	{
		CameraLoc = SafeLoc + (CameraLoc - SafeLoc) * DistBlockedPct;
	}
}
