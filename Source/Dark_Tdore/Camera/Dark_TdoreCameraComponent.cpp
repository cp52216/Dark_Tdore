// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCameraComponent.h"
#include "Dark_TdoreCameraMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCameraComponent)

UDark_TdoreCameraComponent::UDark_TdoreCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDark_TdoreCameraComponent::OnRegister()
{
	Super::OnRegister();

	// 组件注册时创建 CameraModeStack（Outer = this）
	if (!CameraModeStack)
	{
		CameraModeStack = NewObject<UDark_TdoreCameraModeStack>(this);
	}
}

// 每帧核心：计算最终摄像机视图并输出给引擎
void UDark_TdoreCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	check(CameraModeStack);

	// 1. 查询委托并推入当前模式
	UpdateCameraModes();

	// 2. 栈求值：更新所有模式 + 混合
	FDark_TdoreCameraModeView CameraModeView;
	CameraModeStack->EvaluateStack(DeltaTime, CameraModeView);

	// 3. 同步 PlayerController 的 ControlRotation（决定移动方向）
	if (APawn* TargetPawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = TargetPawn->GetController<APlayerController>())
		{
			PC->SetControlRotation(CameraModeView.ControlRotation);
		}
	}

	// 4. 应用一次性 FOV 偏移后清零
	CameraModeView.FieldOfView += FieldOfViewOffset;
	FieldOfViewOffset = 0.0f;

	// 5. 同步组件自身的 Transform（调试/其他系统查询用）
	SetWorldLocationAndRotation(CameraModeView.Location, CameraModeView.Rotation);
	FieldOfView = CameraModeView.FieldOfView;

	// 6. 填充引擎渲染用的 FMinimalViewInfo
	DesiredView.Location = CameraModeView.Location;
	DesiredView.Rotation = CameraModeView.Rotation;
	DesiredView.FOV = CameraModeView.FieldOfView;
	DesiredView.OrthoWidth = OrthoWidth;
	DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
	DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;
	DesiredView.AspectRatio = AspectRatio;
	DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
	DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
	DesiredView.ProjectionMode = ProjectionMode;
	DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;
	if (PostProcessBlendWeight > 0.0f)
	{
		DesiredView.PostProcessSettings = PostProcessSettings;
	}
}

void UDark_TdoreCameraComponent::UpdateCameraModes()
{
	check(CameraModeStack);

	if (CameraModeStack->IsStackActivate())
	{
		if (DetermineCameraModeDelegate.IsBound())
		{
			// 委托由 HeroComponent 绑定，返回 DetermineCameraMode() 的结果
			if (const TSubclassOf<UDark_TdoreCameraMode> CameraMode = DetermineCameraModeDelegate.Execute())
			{
				// PushCameraMode 内部处理去重 + 混合初始化
				CameraModeStack->PushCameraMode(CameraMode);
			}
		}
	}
}

void UDark_TdoreCameraComponent::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
	check(CameraModeStack);
	CameraModeStack->GetBlendInfo(OutWeightOfTopLayer, OutTagOfTopLayer);
}
