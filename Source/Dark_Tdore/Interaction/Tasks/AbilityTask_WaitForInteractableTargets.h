// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Abilities/Tasks/AbilityTask.h"
#include "Engine/CollisionProfile.h"
#include "Interaction/Dark_TdoreInteractionOption.h"
#include "AbilityTask_WaitForInteractableTargets.generated.h"

struct FDark_TdoreInteractionQuery;
struct FInteractionQuery;
template <typename T> class TScriptInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteractableObjectsChangedEvent, const TArray<FDark_TdoreInteractionOption>&, InteractableOptions);

/** 交互目标检测基类 — 从摄像机射线提取可交互目标并广播（参考 Lyra UAbilityTask_WaitForInteractableTargets） */
UCLASS(Abstract)
class UAbilityTask_WaitForInteractableTargets : public UAbilityTask
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FInteractableObjectsChangedEvent InteractableObjectsChanged;

protected:
	static void LineTrace(FHitResult& OutHitResult, const UWorld* World, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams Params);

	void AimWithPlayerController(const AActor* InSourceActor, FCollisionQueryParams Params, const FVector& TraceStart, float MaxRange, FVector& OutTraceEnd, bool bIgnorePitch = false) const;

	static bool ClipCameraRayToAbilityRange(FVector CameraLocation, FVector CameraDirection, FVector AbilityCenter, float AbilityRange, FVector& ClippedPosition);

	void UpdateInteractableOptions(const FDark_TdoreInteractionQuery& InteractQuery, const TArray<TScriptInterface<IDark_TdoreInteractableTarget>>& InteractableTargets);

	FCollisionProfileName TraceProfile;
	bool bTraceAffectsAimPitch = true;
	TArray<FDark_TdoreInteractionOption> CurrentOptions;
};
