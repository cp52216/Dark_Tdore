// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Dark_TdoreCombatInputBufferComponent.h"

#include "Dark_Tdore.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreCombatInputBufferComponent)

UDark_TdoreCombatInputBufferComponent::UDark_TdoreCombatInputBufferComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDark_TdoreCombatInputBufferComponent::BufferInputTag(FGameplayTag InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;
	TrimExpiredInputs(CurrentTime);

	FDark_TdoreBufferedCombatInput& NewInput = BufferedInputs.AddDefaulted_GetRef();
	NewInput.InputTag = InputTag;
	NewInput.TimeSeconds = CurrentTime;

	while (BufferedInputs.Num() > MaxBufferedInputs)
	{
		BufferedInputs.RemoveAt(0);
	}

	UE_LOG(LogDark_Tdore, Log, TEXT("[CombatInputBuffer] 缓存输入: Owner=%s Tag=%s BufferNum=%d WindowOpen=%s"),
		*GetNameSafe(GetOwner()),
		*InputTag.ToString(),
		BufferedInputs.Num(),
		IsInputBufferWindowOpen() ? TEXT("true") : TEXT("false"));

	OnInputBuffered.Broadcast(InputTag);
}

void UDark_TdoreCombatInputBufferComponent::OpenInputBufferWindow(FName WindowName)
{
	++OpenWindowCount;

	UE_LOG(LogDark_Tdore, Log, TEXT("[CombatInputBuffer] 打开预输入窗口: Owner=%s Window=%s OpenCount=%d BufferNum=%d"),
		*GetNameSafe(GetOwner()),
		*WindowName.ToString(),
		OpenWindowCount,
		BufferedInputs.Num());

	OnInputBufferWindowOpened.Broadcast(WindowName);
}

void UDark_TdoreCombatInputBufferComponent::CloseInputBufferWindow(FName WindowName)
{
	OpenWindowCount = FMath::Max(0, OpenWindowCount - 1);

	UE_LOG(LogDark_Tdore, Log, TEXT("[CombatInputBuffer] 关闭预输入窗口: Owner=%s Window=%s OpenCount=%d BufferNum=%d"),
		*GetNameSafe(GetOwner()),
		*WindowName.ToString(),
		OpenWindowCount,
		BufferedInputs.Num());

	OnInputBufferWindowClosed.Broadcast(WindowName);
}

bool UDark_TdoreCombatInputBufferComponent::TryConsumeBufferedInput(const FGameplayTagContainer& AllowedInputTags, FGameplayTag& OutInputTag)
{
	OutInputTag = FGameplayTag();
	if (!IsInputBufferWindowOpen() || AllowedInputTags.IsEmpty())
	{
		UE_LOG(LogDark_Tdore, Verbose, TEXT("[CombatInputBuffer] 消费失败: Owner=%s WindowOpen=%s AllowedEmpty=%s BufferNum=%d"),
			*GetNameSafe(GetOwner()),
			IsInputBufferWindowOpen() ? TEXT("true") : TEXT("false"),
			AllowedInputTags.IsEmpty() ? TEXT("true") : TEXT("false"),
			BufferedInputs.Num());
		return false;
	}

	const UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;
	TrimExpiredInputs(CurrentTime);

	for (int32 Index = BufferedInputs.Num() - 1; Index >= 0; --Index)
	{
		const FDark_TdoreBufferedCombatInput& BufferedInput = BufferedInputs[Index];
		if (AllowedInputTags.HasTagExact(BufferedInput.InputTag))
		{
			OutInputTag = BufferedInput.InputTag;
			BufferedInputs.RemoveAt(Index);
			UE_LOG(LogDark_Tdore, Log, TEXT("[CombatInputBuffer] 消费输入成功: Owner=%s Tag=%s Remain=%d"),
				*GetNameSafe(GetOwner()),
				*OutInputTag.ToString(),
				BufferedInputs.Num());
			return true;
		}
	}

	UE_LOG(LogDark_Tdore, Log, TEXT("[CombatInputBuffer] 消费失败: Owner=%s Allowed=%s BufferNum=%d"),
		*GetNameSafe(GetOwner()),
		*AllowedInputTags.ToStringSimple(),
		BufferedInputs.Num());

	return false;
}

void UDark_TdoreCombatInputBufferComponent::ClearBufferedInputs()
{
	BufferedInputs.Reset();
}

void UDark_TdoreCombatInputBufferComponent::TrimExpiredInputs(float CurrentTime)
{
	for (int32 Index = BufferedInputs.Num() - 1; Index >= 0; --Index)
	{
		if ((CurrentTime - BufferedInputs[Index].TimeSeconds) > BufferDuration)
		{
			BufferedInputs.RemoveAt(Index);
		}
	}
}
