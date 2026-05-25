// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCameraMode.h"
#include "Dark_TdoreCameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCameraMode)

// ============ FDark_TdoreCameraModeView — 视图混合 ============

void FDark_TdoreCameraModeView::Blend(const FDark_TdoreCameraModeView& Other, float OtherWeight)
{
	if (OtherWeight <= 0.0f) return;

	if (OtherWeight >= 1.0f)
	{
		*this = Other;
		return;
	}

	// Location: 线性插值
	Location = FMath::Lerp(Location, Other.Location, OtherWeight);

	// Rotation: 加权旋转插值（取归一化差值后加权累加）
	const FRotator DeltaRotation = (Other.Rotation - Rotation).GetNormalized();
	Rotation = Rotation + (OtherWeight * DeltaRotation);

	// ControlRotation: 同 Rotation 处理
	const FRotator DeltaControlRotation = (Other.ControlRotation - ControlRotation).GetNormalized();
	ControlRotation = ControlRotation + (OtherWeight * DeltaControlRotation);

	// FOV: 线性插值
	FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, OtherWeight);
}

// ============ UDark_TdoreCameraMode — 模式基类 ============

UDark_TdoreCameraMode::UDark_TdoreCameraMode()
	: BlendAlpha(1.0f)
	, BlendWeight(1.0f)
	, bResetInterpolation(false)
{
}

UDark_TdoreCameraComponent* UDark_TdoreCameraMode::GetDarkTdoreCameraComponent() const
{
	// CameraMode 的 Outer 是 CameraComponent
	return CastChecked<UDark_TdoreCameraComponent>(GetOuter());
}

UWorld* UDark_TdoreCameraMode::GetWorld() const
{
	// CDO 没有 World，实例通过 Outer 获取
	return HasAnyFlags(RF_ClassDefaultObject) ? nullptr : GetOuter()->GetWorld();
}

AActor* UDark_TdoreCameraMode::GetTargetActor() const
{
	// 目标 Actor = CameraComponent 的 Owner（即 Pawn）
	return GetDarkTdoreCameraComponent()->GetOwner();
}

FVector UDark_TdoreCameraMode::GetPivotLocation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		// Character: 胶囊体顶部 + 蹲伏高度调整 + 眼睛高度
		const ACharacter* TargetCharacterCDO = TargetCharacter->GetClass()->GetDefaultObject<ACharacter>();
		check(TargetCharacterCDO);

		const UCapsuleComponent* CapsuleComp = TargetCharacter->GetCapsuleComponent();
		check(CapsuleComp);

		const UCapsuleComponent* CapsuleCompCDO = TargetCharacterCDO->GetCapsuleComponent();
		check(CapsuleCompCDO);

		// 蹲伏时胶囊体变矮，通过 CDO 和当前差值计算偏移
		const float DefaultHalfHeight = CapsuleCompCDO->GetUnscaledCapsuleHalfHeight();
		const float ActualHalfHeight = CapsuleComp->GetUnscaledCapsuleHalfHeight();
		const float HeightAdjustment = (DefaultHalfHeight - ActualHalfHeight) + TargetCharacterCDO->BaseEyeHeight;

		return TargetCharacter->GetActorLocation() + (FVector::UpVector * HeightAdjustment);
	}

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetPawnViewLocation();
	}

	return TargetActor->GetActorLocation();
}

FRotator UDark_TdoreCameraMode::GetPivotRotation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetViewRotation(); // 由 PlayerController::ControlRotation 驱动
	}

	return TargetActor->GetActorRotation();
}

void UDark_TdoreCameraMode::UpdateCameraMode(float DeltaTime)
{
	UpdateView(DeltaTime);        // 子类计算 View.Location / Rotation / FOV
	UpdateBlending(DeltaTime);    // 基类推进 BlendAlpha → BlendWeight
}

void UDark_TdoreCameraMode::UpdateView(float DeltaTime)
{
	// 默认实现：摄像机放在 Pivot 位置，直接看 PivotRotation
	FVector PivotLocation = GetPivotLocation();
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	View.Location = PivotLocation;
	View.Rotation = PivotRotation;
	View.ControlRotation = View.Rotation;
	View.FieldOfView = FieldOfView;
}

void UDark_TdoreCameraMode::SetBlendWeight(float Weight)
{
	// PushCameraMode 时直接设定初始权重，需要逆推 BlendAlpha
	BlendWeight = FMath::Clamp(Weight, 0.0f, 1.0f);

	const float InvExponent = (BlendExponent > 0.0f) ? (1.0f / BlendExponent) : 1.0f;

	switch (BlendFunction)
	{
	case EDark_TdoreCameraBlendFunction::Linear:
		BlendAlpha = BlendWeight;
		break;
	case EDark_TdoreCameraBlendFunction::EaseIn:
		BlendAlpha = FMath::InterpEaseIn(0.0f, 1.0f, BlendWeight, InvExponent);
		break;
	case EDark_TdoreCameraBlendFunction::EaseOut:
		BlendAlpha = FMath::InterpEaseOut(0.0f, 1.0f, BlendWeight, InvExponent);
		break;
	case EDark_TdoreCameraBlendFunction::EaseInOut:
		BlendAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, BlendWeight, InvExponent);
		break;
	default: break;
	}
}

void UDark_TdoreCameraMode::UpdateBlending(float DeltaTime)
{
	// 线性推进 BlendAlpha = 累计时间 / BlendTime
	if (BlendTime > 0.0f)
	{
		BlendAlpha += (DeltaTime / BlendTime);
		BlendAlpha = FMath::Min(BlendAlpha, 1.0f);
	}
	else
	{
		BlendAlpha = 1.0f; // BlendTime=0 时瞬间完成
	}

	// 将 BlendAlpha 通过曲线函数映射为 BlendWeight
	const float Exponent = (BlendExponent > 0.0f) ? BlendExponent : 1.0f;

	switch (BlendFunction)
	{
	case EDark_TdoreCameraBlendFunction::Linear:
		BlendWeight = BlendAlpha;
		break;
	case EDark_TdoreCameraBlendFunction::EaseIn:
		BlendWeight = FMath::InterpEaseIn(0.0f, 1.0f, BlendAlpha, Exponent);
		break;
	case EDark_TdoreCameraBlendFunction::EaseOut:
		BlendWeight = FMath::InterpEaseOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;
	case EDark_TdoreCameraBlendFunction::EaseInOut:
		BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;
	default: break;
	}
}

// ============ UDark_TdoreCameraModeStack — 模式栈管理 ============

UDark_TdoreCameraModeStack::UDark_TdoreCameraModeStack()
{
}

void UDark_TdoreCameraModeStack::ActivateStack()
{
	if (!bIsActive)
	{
		bIsActive = true;
		// 通知栈内所有模式：栈已激活
		for (UDark_TdoreCameraMode* CameraMode : CameraModeStack)
		{
			check(CameraMode);
			CameraMode->OnActivation();
		}
	}
}

void UDark_TdoreCameraModeStack::DeactivateStack()
{
	if (bIsActive)
	{
		bIsActive = false;
		// 通知栈内所有模式：栈已停用
		for (UDark_TdoreCameraMode* CameraMode : CameraModeStack)
		{
			check(CameraMode);
			CameraMode->OnDeactivation();
		}
	}
}

void UDark_TdoreCameraModeStack::PushCameraMode(TSubclassOf<UDark_TdoreCameraMode> CameraModeClass)
{
	if (!CameraModeClass) return;

	UDark_TdoreCameraMode* CameraMode = GetCameraModeInstance(CameraModeClass);
	check(CameraMode);

	int32 StackSize = CameraModeStack.Num();

	// 如果已是栈顶，无需操作
	if ((StackSize > 0) && (CameraModeStack[0] == CameraMode))
	{
		return;
	}

	// 查找该模式是否已在栈中某处，计算其剩余贡献度
	int32 ExistingStackIndex = INDEX_NONE;
	float ExistingStackContribution = 1.0f;

	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		if (CameraModeStack[StackIndex] == CameraMode)
		{
			ExistingStackIndex = StackIndex;
			ExistingStackContribution *= CameraMode->GetBlendWeight();
			break;
		}
		else
		{
			// 该位置被其他模式覆盖了一部分
			ExistingStackContribution *= (1.0f - CameraModeStack[StackIndex]->GetBlendWeight());
		}
	}

	if (ExistingStackIndex != INDEX_NONE)
	{
		// 已在栈中 → 移出旧位置，保留贡献度作为新初始权重
		CameraModeStack.RemoveAt(ExistingStackIndex);
		StackSize--;
	}
	else
	{
		// 新模式 → 从 0 开始
		ExistingStackContribution = 0.0f;
	}

	// BlendTime > 0 且栈非空 → 平滑过渡；否则直接到 1.0
	const bool bShouldBlend = ((CameraMode->GetBlendTime() > 0.0f) && (StackSize > 0));
	const float NewBlendWeight = (bShouldBlend ? ExistingStackContribution : 1.0f);

	CameraMode->SetBlendWeight(NewBlendWeight);

	// 插入栈顶（索引 0）
	CameraModeStack.Insert(CameraMode, 0);

	// 栈底始终完全贡献
	CameraModeStack.Last()->SetBlendWeight(1.0f);

	if (ExistingStackIndex == INDEX_NONE)
	{
		CameraMode->OnActivation();
	}
}

bool UDark_TdoreCameraModeStack::EvaluateStack(float DeltaTime, FDark_TdoreCameraModeView& OutCameraModeView)
{
	if (!bIsActive) return false;

	UpdateStack(DeltaTime);        // 更新所有模式 + 清理旧模式
	BlendStack(OutCameraModeView); // 从底向上混合
	return true;
}

UDark_TdoreCameraMode* UDark_TdoreCameraModeStack::GetCameraModeInstance(TSubclassOf<UDark_TdoreCameraMode> CameraModeClass)
{
	check(CameraModeClass);

	// 优先从实例池中复用已有的
	for (UDark_TdoreCameraMode* CameraMode : CameraModeInstances)
	{
		if (CameraMode && CameraMode->GetClass() == CameraModeClass)
		{
			return CameraMode;
		}
	}

	// 未找到 → 创建新实例（Outer = CameraComponent）
	UDark_TdoreCameraMode* NewCameraMode = NewObject<UDark_TdoreCameraMode>(GetOuter(), CameraModeClass, NAME_None, RF_NoFlags);
	check(NewCameraMode);

	CameraModeInstances.Add(NewCameraMode);
	return NewCameraMode;
}

void UDark_TdoreCameraModeStack::UpdateStack(float DeltaTime)
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0) return;

	int32 RemoveCount = 0;
	int32 RemoveIndex = INDEX_NONE;

	// 从上到下更新每个模式
	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		UDark_TdoreCameraMode* CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);

		CameraMode->UpdateCameraMode(DeltaTime);

		// 当某个模式 BlendWeight >= 1.0 → 它下方所有模式被完全覆盖，可移除
		if (CameraMode->GetBlendWeight() >= 1.0f)
		{
			RemoveIndex = (StackIndex + 1);
			RemoveCount = (StackSize - RemoveIndex);
			break;
		}
	}

	if (RemoveCount > 0)
	{
		// 通知被移除的模式
		for (int32 StackIndex = RemoveIndex; StackIndex < StackSize; ++StackIndex)
		{
			CameraModeStack[StackIndex]->OnDeactivation();
		}
		CameraModeStack.RemoveAt(RemoveIndex, RemoveCount);
	}
}

void UDark_TdoreCameraModeStack::BlendStack(FDark_TdoreCameraModeView& OutCameraModeView) const
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0) return;

	// 从栈底开始（索引最后）→ 向上逐层混合
	const UDark_TdoreCameraMode* CameraMode = CameraModeStack[StackSize - 1];
	OutCameraModeView = CameraMode->GetCameraModeView();

	for (int32 StackIndex = (StackSize - 2); StackIndex >= 0; --StackIndex)
	{
		CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);
		OutCameraModeView.Blend(CameraMode->GetCameraModeView(), CameraMode->GetBlendWeight());
	}
}

void UDark_TdoreCameraModeStack::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
	if (CameraModeStack.Num() == 0)
	{
		OutWeightOfTopLayer = 1.0f;
		OutTagOfTopLayer = FGameplayTag();
		return;
	}

	const UDark_TdoreCameraMode* TopEntry = CameraModeStack.Last();
	OutWeightOfTopLayer = TopEntry->GetBlendWeight();
	OutTagOfTopLayer = TopEntry->GetCameraTypeTag();
}
