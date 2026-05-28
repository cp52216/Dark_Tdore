// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilityTask_WaitForInteractableTargets_SingleLineTrace.h"
#include "Interaction/Dark_TdoreInteractionStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbilityTask_WaitForInteractableTargets_SingleLineTrace)

UAbilityTask_WaitForInteractableTargets_SingleLineTrace::UAbilityTask_WaitForInteractableTargets_SingleLineTrace(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) {}

// ============ WaitForInteractableTargets_SingleLineTrace — 静态工厂 ============

UAbilityTask_WaitForInteractableTargets_SingleLineTrace*
UAbilityTask_WaitForInteractableTargets_SingleLineTrace::WaitForInteractableTargets_SingleLineTrace(
	UGameplayAbility* OwningAbility, FDark_TdoreInteractionQuery InteractionQuery,
	FCollisionProfileName TraceProfile, FGameplayAbilityTargetingLocationInfo StartLocation,
	float InteractionScanRange, float InteractionScanRate, bool bShowDebug)
{
	UAbilityTask_WaitForInteractableTargets_SingleLineTrace* MyObj =
		NewAbilityTask<UAbilityTask_WaitForInteractableTargets_SingleLineTrace>(OwningAbility);
	MyObj->InteractionScanRange = InteractionScanRange;
	MyObj->InteractionScanRate = InteractionScanRate;
	MyObj->StartLocation = StartLocation;
	MyObj->InteractionQuery = InteractionQuery;
	MyObj->TraceProfile = TraceProfile;
	MyObj->bShowDebug = bShowDebug;
	return MyObj;
}

// ============ Activate — 启动 Timer ============

void UAbilityTask_WaitForInteractableTargets_SingleLineTrace::Activate()
{
	SetWaitingOnAvatar();
	UWorld* World = GetWorld();
	World->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::PerformTrace, InteractionScanRate, true);
}

// ============ OnDestroy — 清理 Timer ============

void UAbilityTask_WaitForInteractableTargets_SingleLineTrace::OnDestroy(bool AbilityEnded)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}
	Super::OnDestroy(AbilityEnded);
}

// ============ PerformTrace — 核心射线检测 ============
// 每 InteractionScanRate 秒执行一次
//
// 流程：
//   1. 获取 Avatar Actor
//   2. AimWithPlayerController → 从摄像机发射射线（考虑碰撞修正）
//   3. LineTrace → 沿射线方向检测
//   4. AppendInteractableTargetsFromHitResult → 提取 IInteractableTarget
//   5. UpdateInteractableOptions → 收集选项 + 过滤可激活的 + 通知 Delegate

void UAbilityTask_WaitForInteractableTargets_SingleLineTrace::PerformTrace()
{
	AActor* AvatarActor = Ability->GetCurrentActorInfo()->AvatarActor.Get();
	if (!AvatarActor) return;

	UWorld* World = GetWorld();

	// 碰撞查询参数：忽略 Avatar 自身（不检测自己）
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(AvatarActor);

	FCollisionQueryParams Params(
		SCENE_QUERY_STAT(UAbilityTask_WaitForInteractableTargets_SingleLineTrace), false);
	Params.AddIgnoredActors(ActorsToIgnore);

	// Step 1: 计算射线起点 → 终点（从摄像机视角）
	FVector TraceStart = StartLocation.GetTargetingTransform().GetLocation();
	FVector TraceEnd;
	AimWithPlayerController(AvatarActor, Params, TraceStart, InteractionScanRange, OUT TraceEnd);

	// Step 2: 执行射线检测
	FHitResult OutHitResult;
	LineTrace(OutHitResult, World, TraceStart, TraceEnd, TraceProfile.Name, Params);

	// Step 3: 从命中结果提取交互目标
	TArray<TScriptInterface<IDark_TdoreInteractableTarget>> InteractableTargets;
	UDark_TdoreInteractionStatics::AppendInteractableTargetsFromHitResult(OutHitResult, InteractableTargets);

	// Step 4: 更新交互选项（收集 → 过滤 → 通知）
	UpdateInteractableOptions(InteractionQuery, InteractableTargets);

	// ===== Debug 可视化 =====
#if ENABLE_DRAW_DEBUG
	if (bShowDebug)
	{
		FColor DebugColor = OutHitResult.bBlockingHit ? FColor::Red : FColor::Green;
		if (OutHitResult.bBlockingHit)
		{
			DrawDebugLine(World, TraceStart, OutHitResult.Location, DebugColor, false, InteractionScanRate);
			DrawDebugSphere(World, OutHitResult.Location, 5, 16, DebugColor, false, InteractionScanRate);
		}
		else
		{
			DrawDebugLine(World, TraceStart, TraceEnd, DebugColor, false, InteractionScanRate);
		}
	}
#endif
}
