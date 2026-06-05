// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Dark_TdoreCombatInputBufferComponent.h"

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
}

void UDark_TdoreCombatInputBufferComponent::OpenInputBufferWindow(FName WindowName)
{
	++OpenWindowCount;
}

void UDark_TdoreCombatInputBufferComponent::CloseInputBufferWindow(FName WindowName)
{
	OpenWindowCount = FMath::Max(0, OpenWindowCount - 1);
}

bool UDark_TdoreCombatInputBufferComponent::TryConsumeBufferedInput(const FGameplayTagContainer& AllowedInputTags, FGameplayTag& OutInputTag)
{
	OutInputTag = FGameplayTag();
	if (!IsInputBufferWindowOpen() || AllowedInputTags.IsEmpty())
	{
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
			return true;
		}
	}

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

