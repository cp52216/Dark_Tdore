// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Dark_TdoreInteractionStatics.generated.h"

class AActor;
class IDark_TdoreInteractableTarget;
struct FHitResult;
struct FOverlapResult;

/**
 * UDark_TdoreInteractionStatics — 交互静态工具函数（参考 Lyra UInteractionStatics）
 *
 * 提供从 Actor/Overlap/HitResult 提取 IInteractableTarget 的工具函数。
 * 蓝图和 C++ 均可调用。
 *
 * 查找逻辑：Actor 自身 + Actor 上所有实现了接口的 Component
 */
UCLASS()
class UDark_TdoreInteractionStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 从交互目标获取对应的 Actor */
	UFUNCTION(BlueprintCallable)
	static AActor* GetActorFromInteractableTarget(TScriptInterface<IDark_TdoreInteractableTarget> InteractableTarget);

	/** 获取 Actor（自身 + 组件）上所有实现了 IInteractableTarget 的接口 */
	UFUNCTION(BlueprintCallable)
	static void GetInteractableTargetsFromActor(AActor* Actor,
		TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& OutInteractableTargets);

	/** 从 Overlap 结果中提取交互目标 */
	static void AppendInteractableTargetsFromOverlapResults(const TArray<FOverlapResult>& OverlapResults,
		TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& OutInteractableTargets);

	/** 从 LineTrace 命中结果中提取交互目标 */
	static void AppendInteractableTargetsFromHitResult(const FHitResult& HitResult,
		TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& OutInteractableTargets);
};
