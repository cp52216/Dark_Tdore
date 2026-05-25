// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"

#include "Dark_TdoreCameraMode.generated.h"

class AActor;
class UCanvas;
class UDark_TdoreCameraComponent;

// ========================================================================
// EDark_TdoreCameraBlendFunction — 摄像机混合函数枚举（参考 Lyra）
// ========================================================================

/**
 * 摄像机模式切换时的混合曲线类型。
 *
 * 当新模式 PushCameraMode 到栈顶后，BlendWeight 从 0 渐变到 1，
 * 渐变曲线由 BlendFunction + BlendExponent 决定。
 *
 * Linear     — 线性插值，权重匀速增长
 * EaseIn     — 缓慢加速进入目标
 * EaseOut    — 快速接近目标后缓慢收尾（最常用，默认）
 * EaseInOut  — 先加速后减速，两端平滑
 */
UENUM(BlueprintType)
enum class EDark_TdoreCameraBlendFunction : uint8
{
	Linear      UMETA(DisplayName = "线性"),
	EaseIn      UMETA(DisplayName = "缓入"),
	EaseOut     UMETA(DisplayName = "缓出"),
	EaseInOut   UMETA(DisplayName = "缓入缓出"),

	COUNT UMETA(Hidden)
};

// ========================================================================
// FDark_TdoreCameraModeView — 视图数据容器（参考 Lyra FLyraCameraModeView）
// ========================================================================

/**
 * 单个摄像机模式输出的视图数据。
 * 栈内多个模式通过 Blend() 按权重混合得到最终视图。
 *
 * Location        — 摄像机世界位置
 * Rotation        — 摄像机朝向
 * ControlRotation — 同步给 PlayerController 的控制旋转（影响移动方向）
 * FieldOfView     — 视场角（度）
 */
USTRUCT(BlueprintType)
struct FDark_TdoreCameraModeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly)
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly)
	float FieldOfView = 80.0f;

	/**
	 * 按权重将 Other 混合到 this 上。
	 * OtherWeight=0 → 不变；OtherWeight=1 → 完全替换为 Other
	 * Location 做 Lerp，Rotation 做加权旋转插值，FOV 做 Lerp
	 */
	void Blend(const FDark_TdoreCameraModeView& Other, float OtherWeight);
};

// ========================================================================
// UDark_TdoreCameraMode — 摄像机模式基类（参考 Lyra ULyraCameraMode）
// ========================================================================

/**
 * 所有摄像机模式的抽象基类（UObject，非 Blueprintable）。
 * 子类（如第三人称、第一人称、瞄准镜）覆写 UpdateView 实现各自的视角计算。
 *
 * 属性分为两组：
 *   View      — FieldOfView / ViewPitchMin / ViewPitchMax
 *   Blending  — BlendTime / BlendFunction / BlendExponent / CameraTypeTag
 *
 * 生命周期（由 CameraModeStack 调用）：
 *   OnActivation()   → 推入栈时
 *   UpdateCameraMode → 每帧 UpdateView + UpdateBlending
 *   OnDeactivation() → 移出栈时
 */
UCLASS(MinimalAPI, Abstract, NotBlueprintable)
class UDark_TdoreCameraMode : public UObject
{
	GENERATED_BODY()

public:
	UDark_TdoreCameraMode();

	/** 获取拥有此模式的 CameraComponent（作为 Outer） */
	UDark_TdoreCameraComponent* GetDarkTdoreCameraComponent() const;

	virtual UWorld* GetWorld() const override;

	/** 获取摄像机目标 Actor（即 Pawn） */
	AActor* GetTargetActor() const;

	/** 获取当前计算的视图 */
	const FDark_TdoreCameraModeView& GetCameraModeView() const { return View; }

	/** 模式被推入摄像机栈（激活）时回调 */
	virtual void OnActivation() {}

	/** 模式从摄像机栈中被移除（停用）时回调 */
	virtual void OnDeactivation() {}

	/** 每帧更新视图 + 混合权重（由 CameraModeStack 调用） */
	void UpdateCameraMode(float DeltaTime);

	float GetBlendTime() const { return BlendTime; }
	float GetBlendWeight() const { return BlendWeight; }

	/** 直接设置混合权重（PushCameraMode 时使用） */
	void SetBlendWeight(float Weight);

	/** 摄像机类型 Tag，可用于查询当前激活的相机种类（如瞄准镜） */
	FGameplayTag GetCameraTypeTag() const { return CameraTypeTag; }

protected:
	/**
	 * 计算 Pivot 位置（视线起点）。
	 * Character: 胶囊体顶部 + 蹲伏调整
	 * Pawn:      GetPawnViewLocation
	 * 其他:      GetActorLocation
	 */
	virtual FVector GetPivotLocation() const;

	/** 计算 Pivot 旋转（使用 Controller 的 ViewRotation） */
	virtual FRotator GetPivotRotation() const;

	/** 子类覆写：计算 View.Location / Rotation / FOV */
	virtual void UpdateView(float DeltaTime);

	/** 根据 BlendTime + BlendFunction 推进 BlendAlpha → BlendWeight */
	virtual void UpdateBlending(float DeltaTime);

protected:
	/** 摄像机类型标识 Tag（如瞄准时查询 CameraTypeTag == "Camera.Aim"） */
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	FGameplayTag CameraTypeTag;

	/** 当前视图输出 */
	FDark_TdoreCameraModeView View;

	/** 水平视场角（度），5~170 */
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "5.0", UIMax = "170", ClampMin = "5.0", ClampMax = "170.0"))
	float FieldOfView = 80.0f;

	/** 最小俯仰角（度），-89.9~89.9 */
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "-89.9", UIMax = "89.9"))
	float ViewPitchMin = -89.0f;

	/** 最大俯仰角（度），-89.9~89.9 */
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "-89.9", UIMax = "89.9"))
	float ViewPitchMax = 89.0f;

	/** 混合过渡时长（秒）。0 = 瞬间切换，0.5 = 半秒平滑过渡 */
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendTime = 0.5f;

	/** 混合曲线函数 */
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	EDark_TdoreCameraBlendFunction BlendFunction = EDark_TdoreCameraBlendFunction::EaseOut;

	/** 混合曲线指数（控制 EaseIn/EaseOut/EaseInOut 的弯曲程度） */
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendExponent = 4.0f;

	/** 线性混合 alpha（0→1），每帧 BlendTime > 0 时累加 DeltaTime / BlendTime */
	float BlendAlpha = 1.0f;

	/** 混合权重（0→1），由 BlendAlpha 经过 BlendFunction + BlendExponent 映射得到 */
	float BlendWeight = 1.0f;

protected:
	/** 跳过插值标志：下一帧直接跳到目标位置（用于初始化首帧） */
	UPROPERTY(transient)
	uint32 bResetInterpolation : 1;
};

// ========================================================================
// UDark_TdoreCameraModeStack — 摄像机模式栈（参考 Lyra ULyraCameraModeStack）
// ========================================================================

/**
 * 管理多个 CameraMode 的 Push / 混合 / 移除。
 *
 * 工作原理：
 *   PushCameraMode(Class) → 推入栈顶，BlendWeight 从 0 渐变到 1
 *   UpdateStack(DeltaTime) → 每模式 UpdateView + UpdateBlending
 *   BlendStack(OutView)     → 从栈底向上：Base.Blend(Mid, w1).Blend(Top, w2)
 *
 * 栈底始终 BlendWeight = 1.0（完全贡献）。
 * 当顶部模式 BlendWeight >= 1.0 时，栈底所有旧模式自动移除。
 *
 * 用途：
 *   正常:       Stack = [第三人称]
 *   技能覆盖:   Stack = [瞄准特写, 第三人称]  ← 上方的 BlendWeight 0→1 平滑过渡
 *   大招结束:   Stack = [第三人称]            ← 瞄准模式移除后自动恢复
 */
UCLASS()
class UDark_TdoreCameraModeStack : public UObject
{
	GENERATED_BODY()

public:
	UDark_TdoreCameraModeStack();

	/** 激活栈（恢复时通知所有模式） */
	void ActivateStack();

	/** 停用栈（通知所有模式 OnDeactivation） */
	void DeactivateStack();

	bool IsStackActivate() const { return bIsActive; }

	/**
	 * 推入摄像机模式到栈顶。
	 * 如果该模式已存在 → 保留贡献度 → 移到栈顶
	 * 如果 BlendTime > 0 → 初始 BlendWeight = 旧贡献度 → 平滑过渡
	 */
	void PushCameraMode(TSubclassOf<UDark_TdoreCameraMode> CameraModeClass);

	/** 求值：更新所有模式 → 混合输出最终视图 */
	bool EvaluateStack(float DeltaTime, FDark_TdoreCameraModeView& OutCameraModeView);

	/** 获取栈顶模式的混合权重和类型 Tag */
	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

protected:
	/** 获取或创建指定模式的实例（UObject 池，按类复用） */
	UDark_TdoreCameraMode* GetCameraModeInstance(TSubclassOf<UDark_TdoreCameraMode> CameraModeClass);

	/** 每帧更新栈内所有模式，移除权重达 1.0 以上的旧模式 */
	void UpdateStack(float DeltaTime);

	/** 从栈底向上混合：Result = Bottom + Mid*w + Top*w */
	void BlendStack(FDark_TdoreCameraModeView& OutCameraModeView) const;

protected:
	bool bIsActive = true;

	/** 已创建的模式实例池（按 Class 查找复用） */
	UPROPERTY()
	TArray<TObjectPtr<UDark_TdoreCameraMode>> CameraModeInstances;

	/** 当前活跃的摄像机模式栈（索引 0 = 栈顶） */
	UPROPERTY()
	TArray<TObjectPtr<UDark_TdoreCameraMode>> CameraModeStack;
};
