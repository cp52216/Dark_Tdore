// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreInteractionStatics.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Dark_TdoreInteractableTarget.h"
#include "UObject/ScriptInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreInteractionStatics)

// ============ GetActorFromInteractableTarget ============
// 交互目标可能是 Actor 或 Component → 统一提取所属 Actor

AActor* UDark_TdoreInteractionStatics::GetActorFromInteractableTarget(
	TScriptInterface<IDark_TdoreInteractableTarget> InteractableTarget)
{
	if (UObject* Object = InteractableTarget.GetObject())
	{
		if (AActor* Actor = Cast<AActor>(Object)) return Actor;
		if (UActorComponent* Component = Cast<UActorComponent>(Object)) return Component->GetOwner();
	}
	return nullptr;
}

// ============ GetInteractableTargetsFromActor ============
// 检查 Actor 自身 + 所有实现了接口的 Component

void UDark_TdoreInteractionStatics::GetInteractableTargetsFromActor(AActor* Actor,
	TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& OutInteractableTargets)
{
	// Actor 自身实现了接口
	TScriptInterface<IDark_TdoreInteractableTarget> InteractableActor(Actor);
	if (InteractableActor) OutInteractableTargets.Add(InteractableActor);

	// Actor 上的组件实现了接口
	TArray<UActorComponent*> InteractableComponents =
		Actor ? Actor->GetComponentsByInterface(UDark_TdoreInteractableTarget::StaticClass()) : TArray<UActorComponent*>();
	for (UActorComponent* Comp : InteractableComponents)
		OutInteractableTargets.Add(TScriptInterface<IDark_TdoreInteractableTarget>(Comp));
}

// ============ AppendInteractableTargetsFromOverlapResults ============
// 从球形/盒子 Overlap 结果中提取所有交互目标
// 调用链路：GrantNearbyInteraction::QueryInteractables → OverlapMultiByChannel → 此函数

void UDark_TdoreInteractionStatics::AppendInteractableTargetsFromOverlapResults(
	const TArray<FOverlapResult>& OverlapResults,
	TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& OutInteractableTargets)
{
	for (const FOverlapResult& Overlap : OverlapResults)
	{
		// Check both the overlapped actor AND component
		TScriptInterface<IDark_TdoreInteractableTarget> InteractableActor(Overlap.GetActor());
		if (InteractableActor) { OutInteractableTargets.AddUnique(InteractableActor); }

		TScriptInterface<IDark_TdoreInteractableTarget> InteractableComponent(Overlap.GetComponent());
		if (InteractableComponent) { OutInteractableTargets.AddUnique(InteractableComponent); }
	}
}

// ============ AppendInteractableTargetsFromHitResult ============
// 从 LineTrace 命中结果中提取交互目标
// 调用链路：WaitForInteractableTargets_SingleLineTrace::PerformTrace → 此函数

void UDark_TdoreInteractionStatics::AppendInteractableTargetsFromHitResult(
	const FHitResult& HitResult,
	TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& OutInteractableTargets)
{
	TScriptInterface<IDark_TdoreInteractableTarget> InteractableActor(HitResult.GetActor());
	if (InteractableActor) { OutInteractableTargets.AddUnique(InteractableActor); }

	TScriptInterface<IDark_TdoreInteractableTarget> InteractableComponent(HitResult.GetComponent());
	if (InteractableComponent) { OutInteractableTargets.AddUnique(InteractableComponent); }
}
