// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "Dark_TdoreVerbMessageHelpers.generated.h"

struct FGameplayCueParameters;
struct FDark_TdoreVerbMessage;

class APlayerController;
class APlayerState;
class UObject;
struct FFrame;

/**
 * UDark_TdoreVerbMessageHelpers — 消息转换工具
 *
 * 参考 Lyra ULyraVerbMessageHelpers
 *
 * 提供：
 *   GetPlayerStateFromObject — 从任意对象提取 PlayerState
 *   GetPlayerControllerFromObject — 从任意对象提取 PlayerController
 *   VerbMessageToCueParameters — VerbMessage → GameplayCue 参数
 *   CueParametersToVerbMessage — GameplayCue 参数 → VerbMessage
 */
UCLASS(MinimalAPI)
class UDark_TdoreVerbMessageHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 从 PlayerController / PlayerState / Pawn 提取 PlayerState */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Messages")
	static DARK_TDORE_API APlayerState* GetPlayerStateFromObject(UObject* Object);

	/** 从 PlayerController / PlayerState / Pawn 提取 PlayerController */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Messages")
	static DARK_TDORE_API APlayerController* GetPlayerControllerFromObject(UObject* Object);

	/** VerbMessage 转 GameplayCue 参数（用于音效/特效触发） */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Messages")
	static DARK_TDORE_API FGameplayCueParameters VerbMessageToCueParameters(const FDark_TdoreVerbMessage& Message);

	/** GameplayCue 参数转 VerbMessage */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Messages")
	static DARK_TDORE_API FDark_TdoreVerbMessage CueParametersToVerbMessage(const FGameplayCueParameters& Params);
};
