// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/CapsuleComponent.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"
#include "WorldCollision.h"

// ============ 控制台变量 ============

namespace DarkTdoreCharacter
{
	static float GroundTraceDistance = 100000.0f;
	FAutoConsoleVariableRef CVar_GroundTraceDistance(
		TEXT("DarkTdore.GroundTraceDistance"),
		GroundTraceDistance,
		TEXT("地面检测射线距离"),
		ECVF_Cheat);
}

// ============ 构造 ============

UDark_TdoreCharacterMovementComponent::UDark_TdoreCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDark_TdoreCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

// ============ 移动模拟 ============

void UDark_TdoreCharacterMovementComponent::SimulateMovement(float DeltaTime)
{
	if (bHasReplicatedAcceleration)
	{
		// 保留网络复制的加速度，避免被模拟覆盖
		const FVector OriginalAcceleration = Acceleration;
		Super::SimulateMovement(DeltaTime);
		Acceleration = OriginalAcceleration;
	}
	else
	{
		Super::SimulateMovement(DeltaTime);
	}
}

// ============ 跳跃 ============

bool UDark_TdoreCharacterMovementComponent::CanAttemptJump() const
{
	// 与 UCharacterMovementComponent 相同，但移除了蹲伏检查
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling());
}

// ============ ASC 感知的移动控制 ============

FRotator UDark_TdoreCharacterMovementComponent::GetDeltaRotation(float DeltaTime) const
{
	// 如果 ASC 有 Gameplay.MovementStopped 标签，禁止旋转
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Gameplay.MovementStopped"))))
		{
			return FRotator::ZeroRotator;
		}
	}

	return Super::GetDeltaRotation(DeltaTime);
}

float UDark_TdoreCharacterMovementComponent::GetMaxSpeed() const
{
	// 如果 ASC 有 Gameplay.MovementStopped 标签，速度为 0
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Gameplay.MovementStopped"))))
		{
			return 0.0f;
		}
	}

	return Super::GetMaxSpeed();
}

// ============ 地面信息 ============

const FDark_TdoreCharacterGroundInfo& UDark_TdoreCharacterMovementComponent::GetGroundInfo()
{
	// 如果本帧已更新过，直接返回缓存
	if (!CharacterOwner || (GFrameCounter == CachedGroundInfo.LastUpdateFrame))
	{
		return CachedGroundInfo;
	}

	const UCapsuleComponent* CapsuleComp = CharacterOwner->GetCapsuleComponent();
	check(CapsuleComp);

	if (MovementMode == MOVE_Walking)
	{
		// 行走模式直接用 CurrentFloor
		CachedGroundInfo.GroundHitResult = CurrentFloor.HitResult;
		CachedGroundInfo.GroundDistance = 0.0f;
	}
	else
	{
		// 非行走模式：Raycast 向下检测地面
		const float CapsuleHalfHeight = CapsuleComp->GetUnscaledCapsuleHalfHeight();
		const ECollisionChannel CollisionChannel = (UpdatedComponent ? UpdatedComponent->GetCollisionObjectType() : ECC_Pawn);
		const FVector TraceStart(GetActorLocation());
		const FVector TraceEnd(TraceStart.X, TraceStart.Y, TraceStart.Z - DarkTdoreCharacter::GroundTraceDistance - CapsuleHalfHeight);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DarkTdoreCharacterMovementComponent_GetGroundInfo), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(QueryParams, ResponseParam);

		FHitResult HitResult;
		GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, CollisionChannel, QueryParams, ResponseParam);

		CachedGroundInfo.GroundHitResult = HitResult;
		CachedGroundInfo.GroundDistance = DarkTdoreCharacter::GroundTraceDistance;

		if (MovementMode == MOVE_NavWalking)
		{
			CachedGroundInfo.GroundDistance = 0.0f;
		}
		else if (HitResult.bBlockingHit)
		{
			CachedGroundInfo.GroundDistance = FMath::Max(HitResult.Distance - CapsuleHalfHeight, 0.0f);
		}
	}

	CachedGroundInfo.LastUpdateFrame = GFrameCounter;
	return CachedGroundInfo;
}

// ============ 网络加速 ============

void UDark_TdoreCharacterMovementComponent::SetReplicatedAcceleration(const FVector& InAcceleration)
{
	bHasReplicatedAcceleration = true;
	Acceleration = InAcceleration;
}
