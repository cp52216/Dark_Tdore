// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraComponent.h"
#include "Dark_TdoreCameraComponent.generated.h"

class UDark_TdoreCameraMode;
class UDark_TdoreCameraModeStack;
struct FGameplayTag;

/** 摄像机模式选择委托 — 每帧由 HeroComponent 执行，返回当前应使用的 CameraMode 类 */
DECLARE_DELEGATE_RetVal(TSubclassOf<UDark_TdoreCameraMode>, FDarkTdoreCameraModeDelegate);

/**
 * UDark_TdoreCameraComponent — 摄像机组件（参考 Lyra ULyraCameraComponent）
 *
 * 挂载在 Pawn 上，替代传统 SpringArm + Camera 组合。
 * 内部持有 CameraModeStack，每帧通过 DetermineCameraModeDelegate 查询当前
 * 摄像机模式并推入栈，栈自动处理混合过渡（Linear / EaseIn / EaseOut / EaseInOut）。
 *
 * 数据流：
 *   1. HeroComponent 在 InitState 就绪后绑定 DetermineCameraModeDelegate
 *   2. 每帧 GetCameraView → UpdateCameraModes → 执行委托 → PushCameraMode
 *   3. EvaluateStack → 更新所有模式 + 混合权重计算 → 输出最终视图
 *   4. 同步 PlayerController::SetControlRotation（移动方向跟随视线）
 *   5. 技能可通过 HeroComponent::SetAbilityCameraMode 临时覆盖摄像机模式
 *
 * 示例：
 *   正常: Stack = [第三人称]
 *   GA 放大招: GA 调 HeroComp->SetAbilityCameraMode(大招视角) → Stack = [大招, 第三人称]
 *   GA 结束: HeroComp->ClearAbilityCameraMode → Stack = [第三人称] (平滑过渡回)
 */
UCLASS()
class UDark_TdoreCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreCameraComponent(const FObjectInitializer& ObjectInitializer);

	/** 在指定 Actor 上查找 CameraComponent */
	UFUNCTION(BlueprintPure, Category = "DarkTdore|Camera")
	static UDark_TdoreCameraComponent* FindCameraComponent(const AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UDark_TdoreCameraComponent>() : nullptr;
	}

	/**
	 * 摄像机模式选择委托。
	 * HeroComponent 绑定此委托，每帧返回 DetermineCameraMode() 的结果：
	 *   优先 → AbilityCameraMode（技能覆盖）
	 *   默认 → PawnData.DefaultCameraMode
	 */
	FDarkTdoreCameraModeDelegate DetermineCameraModeDelegate;

	/**
	 * 一次性 FOV 偏移（下一帧自动清零）。
	 * 用途：技能效果临时加减 FOV（如冲刺时拉大 FOV），
	 *       每次调用累加，GetCameraView 中应用后自动归零。
	 */
	void AddFieldOfViewOffset(float FovOffset) { FieldOfViewOffset += FovOffset; }

	/** 获取栈顶模式的混合权重和类型 Tag（供外部查询当前相机状态） */
	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

protected:
	/** 组件注册时创建 CameraModeStack */
	virtual void OnRegister() override;

	/**
	 * 每帧核心：UpdateCameraModes → EvaluateStack → 应用到组件 + Controller
	 *
	 * 流程:
	 *   1. Execute DetermineCameraModeDelegate → 获取当前 CameraMode 类
	 *   2. CameraModeStack.PushCameraMode(类)
	 *   3. CameraModeStack.EvaluateStack(DeltaTime, View) → 更新+混合
	 *   4. PlayerController.SetControlRotation(View.ControlRotation)
	 *   5. 应用 FOV 偏移 → 清零
	 *   6. SetWorldLocationAndRotation + FieldOfView = View 输出
	 *   7. 填充 FMinimalViewInfo 给引擎渲染
	 */
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

	/** 每帧查询委托并将返回的模式推入栈 */
	virtual void UpdateCameraModes();

protected:
	/** 摄像机模式栈（管理 Push / 混合 / 移除） */
	UPROPERTY()
	TObjectPtr<UDark_TdoreCameraModeStack> CameraModeStack;

	/** 一次性 FOV 偏移值 */
	float FieldOfViewOffset = 0.0f;
};
