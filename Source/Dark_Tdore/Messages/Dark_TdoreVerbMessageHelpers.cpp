// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreVerbMessageHelpers.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffectTypes.h"
#include "Messages/Dark_TdoreVerbMessage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreVerbMessageHelpers)

// ============ GetPlayerStateFromObject ============
// 从 PlayerController / PlayerState / Pawn 中提取 PlayerState

APlayerState* UDark_TdoreVerbMessageHelpers::GetPlayerStateFromObject(UObject* Object)
{
	if (APlayerController* PC = Cast<APlayerController>(Object))
	{
		return PC->PlayerState;
	}

	if (APlayerState* TargetPS = Cast<APlayerState>(Object))
	{
		return TargetPS;
	}

	if (APawn* TargetPawn = Cast<APawn>(Object))
	{
		if (APlayerState* TargetPS = TargetPawn->GetPlayerState())
		{
			return TargetPS;
		}
	}
	return nullptr;
}

// ============ GetPlayerControllerFromObject ============
// 从 PlayerController / PlayerState / Pawn 中提取 PlayerController

APlayerController* UDark_TdoreVerbMessageHelpers::GetPlayerControllerFromObject(UObject* Object)
{
	if (APlayerController* PC = Cast<APlayerController>(Object))
	{
		return PC;
	}

	if (APlayerState* TargetPS = Cast<APlayerState>(Object))
	{
		return TargetPS->GetPlayerController();
	}

	if (APawn* TargetPawn = Cast<APawn>(Object))
	{
		return Cast<APlayerController>(TargetPawn->GetController());
	}

	return nullptr;
}

// ============ VerbMessageToCueParameters ============
// 将 VerbMessage 转换为 GameplayCueParameters，方便触发音效/特效

FGameplayCueParameters UDark_TdoreVerbMessageHelpers::VerbMessageToCueParameters(
	const FDark_TdoreVerbMessage& Message)
{
	FGameplayCueParameters Result;

	Result.OriginalTag = Message.Verb;
	Result.Instigator = Cast<AActor>(Message.Instigator);
	Result.EffectCauser = Cast<AActor>(Message.Target);
	Result.AggregatedSourceTags = Message.InstigatorTags;
	Result.AggregatedTargetTags = Message.TargetTags;
	Result.RawMagnitude = Message.Magnitude;

	return Result;
}

// ============ CueParametersToVerbMessage ============
// 将 GameplayCueParameters 转回 VerbMessage

FDark_TdoreVerbMessage UDark_TdoreVerbMessageHelpers::CueParametersToVerbMessage(
	const FGameplayCueParameters& Params)
{
	FDark_TdoreVerbMessage Result;

	Result.Verb = Params.OriginalTag;
	Result.Instigator = Params.Instigator.Get();
	Result.Target = Params.EffectCauser.Get();
	Result.InstigatorTags = Params.AggregatedSourceTags;
	Result.TargetTags = Params.AggregatedTargetTags;
	Result.Magnitude = Params.RawMagnitude;

	return Result;
}
