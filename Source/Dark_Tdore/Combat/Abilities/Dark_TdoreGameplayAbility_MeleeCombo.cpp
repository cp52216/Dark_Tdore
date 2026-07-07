// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/Dark_TdoreGameplayAbility_MeleeCombo.h"

#include "Animation/AnimMontage.h"
#include "Combat/Dark_TdoreCombatInputBufferComponent.h"
#include "Dark_Tdore.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "TimerManager.h"
#include "Weapons/Dark_TdoreWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGameplayAbility_MeleeCombo)

UDark_TdoreGameplayAbility_MeleeCombo::UDark_TdoreGameplayAbility_MeleeCombo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationGroup = EDark_TdoreAbilityActivationGroup::Exclusive_Replaceable;
	ActivationPolicy = EDark_TdoreAbilityActivationPolicy::OnInputTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UDark_TdoreWeaponInstance* UDark_TdoreGameplayAbility_MeleeCombo::GetWeaponInstance() const
{
	return Cast<UDark_TdoreWeaponInstance>(GetAssociatedEquipment());
}

UDark_TdoreCombatInputBufferComponent* UDark_TdoreGameplayAbility_MeleeCombo::GetInputBufferComponent() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	return AvatarActor ? AvatarActor->FindComponentByClass<UDark_TdoreCombatInputBufferComponent>() : nullptr;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::TryConsumeNextComboInput(FGameplayTagContainer AllowedInputTags, FGameplayTag& OutInputTag)
{
	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = GetInputBufferComponent())
	{
		return BufferComponent->TryConsumeBufferedInput(AllowedInputTags, OutInputTag);
	}

	OutInputTag = FGameplayTag();
	return false;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::FindComboStepForInput(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const
{
	return ComboData ? ComboData->FindStepForInputTag(InputTag, OutStep) : false;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::TryStartNextComboStepFromBuffer(const FGameplayTagContainer& AllowedInputTags, FDark_TdoreComboStep& OutStep)
{
	OutStep = FDark_TdoreComboStep();

	// 这里把“消费输入 -> 查数据 -> 启动下一段”合成一个函数，
	// 蓝图只需要在动画预输入窗口里调用它，不需要重复写这段样板逻辑。
	FGameplayTag ConsumedInputTag;
	if (!TryConsumeNextComboInput(AllowedInputTags, ConsumedInputTag))
	{
		return false;
	}

	if (!FindNextComboStepForInput(ConsumedInputTag, OutStep))
	{
		return false;
	}

	StartComboStep(OutStep);
	return true;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return GetAssociatedEquipment() != nullptr;
}

void UDark_TdoreGameplayAbility_MeleeCombo::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	BindInputBufferDelegates();

	FDark_TdoreComboStep Step;
	const bool bFoundDefaultStep = ComboData && ComboData->FindStepByName(DefaultStepName, Step);
	if (!bFoundDefaultStep)
	{
		// 配置不完整时立刻结束技能，避免 Ability 已激活但没有动画/结束点，导致输入被长期占用。
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	StartComboStep(Step);
}

void UDark_TdoreGameplayAbility_MeleeCombo::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UnbindInputBufferDelegates();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboStepFinishTimerHandle);
		World->GetTimerManager().ClearTimer(DefaultInputBufferWindowOpenTimerHandle);
		World->GetTimerManager().ClearTimer(DefaultInputBufferWindowCloseTimerHandle);
	}

	CloseDefaultInputBufferWindow();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UDark_TdoreGameplayAbility_MeleeCombo::StartComboStep(const FDark_TdoreComboStep& Step)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !Step.Montage)
	{
		// 没有角色或没有 Montage 时直接结束，避免 Ability 激活后没有任何可见结果。
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	CurrentStep = Step;
	bHasCurrentStep = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboStepFinishTimerHandle);
		World->GetTimerManager().ClearTimer(DefaultInputBufferWindowOpenTimerHandle);
		World->GetTimerManager().ClearTimer(DefaultInputBufferWindowCloseTimerHandle);
	}

	// 如果上一段的默认窗口已经打开，切下一段前先配对关闭，避免 BufferComponent 的 OpenCount 越叠越高。
	CloseDefaultInputBufferWindow();

	// 当前阶段先走 Character::PlayAnimMontage，保证空蓝图 GA 也能播放攻击。
	// 后续做命中窗口/预测时，再升级为 AbilityTask_PlayMontageAndWait。
	const float PlayedLength = Character->PlayAnimMontage(Step.Montage, 1.0f, Step.MontageSection);
	K2_StartComboStep(Step);

	const USkeletalMeshComponent* MeshComponent = Character->GetMesh();
	const UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr;
	// 基础动画信息
	// 输入缓冲窗口配置
	// 攻击参数：轻重、硬直等级、标签、伤害 GE
	// 位移配置：距离、时长、速度曲线
	// 吸附配置：开关、距离范围、MotionWarping 目标名、权重曲线
	UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 播放连招段: Ability=%s Pawn=%s Mesh=%s AnimInstance=%s Step=%s Montage=%s Section=%s Slot=%s PlayedLength=%.3f AllowedNext=%s "
		"InputBufferDefault=%s WindowStart=%.2f WindowDuration=%.3f "
		"AttackWeight=%s ImpactLevel=%s AttackTags=%s DamageEffect=%s "
		"MoveDist=%.1f MoveDuration=%.3f MoveCurve=%s "
		"MagEnabled=%s MagMin=%.1f MagMax=%.1f MagTarget=%s MagCurve=%s"),
		/* 基础动画信息 */ *GetNameSafe(this),
		*GetNameSafe(Character),
		*GetNameSafe(MeshComponent),
		*GetNameSafe(AnimInstance),
		*Step.StepName.ToString(),
		*GetNameSafe(Step.Montage),
		Step.MontageSection.IsNone() ? TEXT("None") : *Step.MontageSection.ToString(),
		Step.Montage->SlotAnimTracks.Num() > 0 ? *Step.Montage->SlotAnimTracks[0].SlotName.ToString() : TEXT("None"),
		PlayedLength,
		*Step.AllowedNextInputTags.ToStringSimple(),
		/* 输入缓冲窗口配置 */ Step.bUseDefaultInputBufferWindow ? TEXT("true") : TEXT("false"),
		Step.DefaultInputBufferWindowStartRatio,
		Step.DefaultInputBufferWindowDuration,
		/* 攻击参数 */ *StaticEnum<EDark_TdoreCombatAttackWeight>()->GetNameStringByValue(static_cast<int64>(Step.AttackWeight)),
		*StaticEnum<EDark_TdoreCombatImpactLevel>()->GetNameStringByValue(static_cast<int64>(Step.ImpactLevel)),
		*Step.AttackTags.ToStringSimple(),
		*GetNameSafe(Step.DamageEffect),
		/* 位移配置 */ Step.Movement.Distance,
		Step.Movement.Duration,
		*GetNameSafe(Step.Movement.SpeedCurve),
		/* 吸附配置 */ Step.Magnetism.bEnableMagnetism ? TEXT("true") : TEXT("false"),
		Step.Magnetism.MinDistance,
		Step.Magnetism.MaxDistance,
		*Step.Magnetism.MotionWarpingTargetName.ToString(),
		*GetNameSafe(Step.Magnetism.WeightCurve));

	if (PlayedLength <= 0.0f)
	{
		// 常见原因：AnimBP 没有对应 Slot、骨骼不匹配、Montage 为空或 Section 不存在。
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ComboStepFinishTimerHandle, this, &ThisClass::HandleComboStepFinished, PlayedLength, false);

		if (!Step.AllowedNextInputTags.IsEmpty() && Step.bUseDefaultInputBufferWindow)
		{
			const float WindowStartTime = FMath::Clamp(Step.DefaultInputBufferWindowStartRatio, 0.0f, 1.0f) * PlayedLength;
			const float WindowDuration = Step.DefaultInputBufferWindowDuration > 0.0f ? Step.DefaultInputBufferWindowDuration : FMath::Max(0.0f, PlayedLength - WindowStartTime);
			World->GetTimerManager().SetTimer(DefaultInputBufferWindowOpenTimerHandle, this, &ThisClass::OpenDefaultInputBufferWindow, WindowStartTime, false);
			World->GetTimerManager().SetTimer(DefaultInputBufferWindowCloseTimerHandle, this, &ThisClass::CloseDefaultInputBufferWindow, WindowStartTime + WindowDuration, false);

			UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 默认预输入窗口已安排: Step=%s Start=%.3f Duration=%.3f"),
				*Step.StepName.ToString(),
				WindowStartTime,
				WindowDuration);
		}
	}
}

void UDark_TdoreGameplayAbility_MeleeCombo::HandleComboStepFinished()
{
	UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 连招段结束: Ability=%s Step=%s"),
		*GetNameSafe(this),
		bHasCurrentStep ? *CurrentStep.StepName.ToString() : TEXT("None"));

	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UDark_TdoreGameplayAbility_MeleeCombo::BindInputBufferDelegates()
{
	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = GetInputBufferComponent())
	{
		BufferComponent->OnInputBuffered.RemoveAll(this);
		BufferComponent->OnInputBufferWindowOpened.RemoveAll(this);
		BufferComponent->OnInputBuffered.AddUObject(this, &ThisClass::HandleBufferedInput);
		BufferComponent->OnInputBufferWindowOpened.AddUObject(this, &ThisClass::HandleInputBufferWindowOpened);
	}
}

void UDark_TdoreGameplayAbility_MeleeCombo::UnbindInputBufferDelegates()
{
	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = GetInputBufferComponent())
	{
		BufferComponent->OnInputBuffered.RemoveAll(this);
		BufferComponent->OnInputBufferWindowOpened.RemoveAll(this);
		BufferComponent->OnInputBufferWindowClosed.RemoveAll(this);
	}
}

void UDark_TdoreGameplayAbility_MeleeCombo::HandleBufferedInput(FGameplayTag InputTag)
{
	UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 收到缓冲输入: Ability=%s Step=%s Input=%s WindowOpen=%s"),
		*GetNameSafe(this),
		bHasCurrentStep ? *CurrentStep.StepName.ToString() : TEXT("None"),
		*InputTag.ToString(),
		GetInputBufferComponent() && GetInputBufferComponent()->IsInputBufferWindowOpen() ? TEXT("true") : TEXT("false"));

	TryStartNextComboStep();
}

void UDark_TdoreGameplayAbility_MeleeCombo::HandleInputBufferWindowOpened(FName WindowName)
{
	UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 预输入窗口打开: Ability=%s Step=%s Window=%s"),
		*GetNameSafe(this),
		bHasCurrentStep ? *CurrentStep.StepName.ToString() : TEXT("None"),
		*WindowName.ToString());

	TryStartNextComboStep();
}

void UDark_TdoreGameplayAbility_MeleeCombo::OpenDefaultInputBufferWindow()
{
	if (bDefaultInputBufferWindowOpen)
	{
		return;
	}

	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = GetInputBufferComponent())
	{
		bDefaultInputBufferWindowOpen = true;
		BufferComponent->OpenInputBufferWindow(TEXT("DefaultComboWindow"));
	}
}

void UDark_TdoreGameplayAbility_MeleeCombo::CloseDefaultInputBufferWindow()
{
	if (!bDefaultInputBufferWindowOpen)
	{
		return;
	}

	if (UDark_TdoreCombatInputBufferComponent* BufferComponent = GetInputBufferComponent())
	{
		BufferComponent->CloseInputBufferWindow(TEXT("DefaultComboWindow"));
	}

	bDefaultInputBufferWindowOpen = false;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::TryStartNextComboStep()
{
	if (!bHasCurrentStep || CurrentStep.AllowedNextInputTags.IsEmpty())
	{
		return false;
	}

	FDark_TdoreComboStep NextStep;
	const FName FromStepName = CurrentStep.StepName;
	const bool bStarted = TryStartNextComboStepFromBuffer(CurrentStep.AllowedNextInputTags, NextStep);
	if (bStarted)
	{
		UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 连招派生成功: Ability=%s From=%s To=%s"),
			*GetNameSafe(this),
			*FromStepName.ToString(),
			*NextStep.StepName.ToString());
	}
	return bStarted;
}

bool UDark_TdoreGameplayAbility_MeleeCombo::FindNextComboStepForInput(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const
{
	if (!ComboData || !InputTag.IsValid())
	{
		return false;
	}

	const TArray<FDark_TdoreComboStep>& Steps = ComboData->GetSteps();
	int32 CurrentStepIndex = INDEX_NONE;

	if (bHasCurrentStep)
	{
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			if (Steps[Index].StepName == CurrentStep.StepName)
			{
				CurrentStepIndex = Index;
				break;
			}
		}
	}

	for (int32 Index = CurrentStepIndex + 1; Index < Steps.Num(); ++Index)
	{
		if (Steps[Index].InputTag == InputTag)
		{
			OutStep = Steps[Index];
			return true;
		}
	}

	UE_LOG(LogDark_Tdore, Log, TEXT("[MeleeCombo] 未找到下一段: Ability=%s Current=%s Input=%s Steps=%d"),
		*GetNameSafe(this),
		bHasCurrentStep ? *CurrentStep.StepName.ToString() : TEXT("None"),
		*InputTag.ToString(),
		Steps.Num());

	return false;
}
