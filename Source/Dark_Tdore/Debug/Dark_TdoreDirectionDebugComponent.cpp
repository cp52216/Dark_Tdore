// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/Dark_TdoreDirectionDebugComponent.h"

#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreDirectionDebugComponent)

namespace DarkTdoreDirectionDebug
{
	static bool bEnabled = true;
	static FAutoConsoleVariableRef CVarDirectionDebugEnabled(
		TEXT("DarkTdore.Debug.DirectionArrows"),
		bEnabled,
		TEXT("Draw movement, actor, and controller direction arrows for pawns with Dark_TdoreDirectionDebugComponent."),
		ECVF_Cheat);
}

UDark_TdoreDirectionDebugComponent::UDark_TdoreDirectionDebugComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

void UDark_TdoreDirectionDebugComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureArrowComponents();
}

void UDark_TdoreDirectionDebugComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyArrowComponents();
	Super::EndPlay(EndPlayReason);
}

void UDark_TdoreDirectionDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	if (!bDrawDebug || !DarkTdoreDirectionDebug::bEnabled)
	{
		UpdateArrowComponent(MovementArrowComponent, MovementLabelComponent, FVector::ZeroVector, FVector::ForwardVector, MovementDirectionColor, 0.0f, TEXT("Move"), false);
		UpdateArrowComponent(ActorArrowComponent, ActorLabelComponent, FVector::ZeroVector, FVector::ForwardVector, ActorDirectionColor, 0.0f, TEXT("Actor"), false);
		UpdateArrowComponent(ControllerArrowComponent, ControllerLabelComponent, FVector::ZeroVector, FVector::ForwardVector, ControllerDirectionColor, 0.0f, TEXT("Controller"), false);
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (bDrawOnlyForLocallyControlledPawn && OwnerPawn && !OwnerPawn->IsLocallyControlled())
	{
		UpdateArrowComponent(MovementArrowComponent, MovementLabelComponent, FVector::ZeroVector, FVector::ForwardVector, MovementDirectionColor, 0.0f, TEXT("Move"), false);
		UpdateArrowComponent(ActorArrowComponent, ActorLabelComponent, FVector::ZeroVector, FVector::ForwardVector, ActorDirectionColor, 0.0f, TEXT("Actor"), false);
		UpdateArrowComponent(ControllerArrowComponent, ControllerLabelComponent, FVector::ZeroVector, FVector::ForwardVector, ControllerDirectionColor, 0.0f, TEXT("Controller"), false);
		return;
	}

	const FVector Origin = GetArrowOrigin();
	EnsureArrowComponents();

	FVector MovementDirection = FVector::ZeroVector;
	const bool bHasMovementDirection = GetMovementDirection(MovementDirection);
	if (bUseArrowComponents)
	{
		UpdateArrowComponent(MovementArrowComponent, MovementLabelComponent, Origin, MovementDirection, MovementDirectionColor, 0.0f, TEXT("Move"), bHasMovementDirection);
	}
	else if (bHasMovementDirection)
	{
		DrawDirectionArrow(Origin, MovementDirection, MovementDirectionColor, 0.0f, TEXT("Move"));
	}

	FVector ActorDirection = FVector::ZeroVector;
	const bool bHasActorDirection = GetActorDirection(ActorDirection);
	if (bUseArrowComponents)
	{
		UpdateArrowComponent(ActorArrowComponent, ActorLabelComponent, Origin, ActorDirection, ActorDirectionColor, 8.0f, TEXT("Actor"), bHasActorDirection);
	}
	else if (bHasActorDirection)
	{
		DrawDirectionArrow(Origin, ActorDirection, ActorDirectionColor, 8.0f, TEXT("Actor"));
	}

	FVector ControllerDirection = FVector::ZeroVector;
	const bool bHasControllerDirection = GetControllerDirection(ControllerDirection);
	if (bUseArrowComponents)
	{
		UpdateArrowComponent(ControllerArrowComponent, ControllerLabelComponent, Origin, ControllerDirection, ControllerDirectionColor, 16.0f, TEXT("Controller"), bHasControllerDirection);
	}
	else if (bHasControllerDirection)
	{
		DrawDirectionArrow(Origin, ControllerDirection, ControllerDirectionColor, 16.0f, TEXT("Controller"));
	}
#endif
}

void UDark_TdoreDirectionDebugComponent::SetDebugDrawEnabled(bool bEnabled)
{
	bDrawDebug = bEnabled;
	SetComponentTickEnabled(bEnabled);
}

FVector UDark_TdoreDirectionDebugComponent::GetArrowOrigin() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return FVector::ZeroVector;
	}

	FVector Origin = OwnerActor->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(OwnerActor))
	{
		if (const UCapsuleComponent* CapsuleComponent = Character->GetCapsuleComponent())
		{
			Origin.Z -= CapsuleComponent->GetScaledCapsuleHalfHeight();
		}
	}

	Origin.Z += GroundOffset;
	return Origin;
}

void UDark_TdoreDirectionDebugComponent::EnsureArrowComponents()
{
	if (!bUseArrowComponents)
	{
		DestroyArrowComponents();
		return;
	}

	AActor* OwnerActor = GetOwner();
	USceneComponent* RootComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	if (!OwnerActor || !RootComponent)
	{
		return;
	}

	auto CreateArrow = [this, OwnerActor, RootComponent](auto& ArrowComponent, auto& LabelComponent, const FName ComponentName, const FName LabelComponentName, const FString& Label, const FColor& Color)
	{
		if (!ArrowComponent)
		{
			ArrowComponent = NewObject<UArrowComponent>(OwnerActor, ComponentName);
			ArrowComponent->SetupAttachment(RootComponent);
			ArrowComponent->SetHiddenInGame(false);
			ArrowComponent->SetVisibility(false);
			ArrowComponent->ArrowColor = Color;
			ArrowComponent->ArrowLength = ArrowLength;
			ArrowComponent->ArrowSize = 1.2f;
			ArrowComponent->bTreatAsASprite = false;
			ArrowComponent->RegisterComponent();
		}

		if (!LabelComponent)
		{
			LabelComponent = NewObject<UTextRenderComponent>(OwnerActor, LabelComponentName);
			LabelComponent->SetupAttachment(RootComponent);
			LabelComponent->SetHiddenInGame(false);
			LabelComponent->SetVisibility(false);
			LabelComponent->SetText(FText::FromString(Label));
			LabelComponent->SetTextRenderColor(Color);
			LabelComponent->SetHorizontalAlignment(EHTA_Center);
			LabelComponent->SetVerticalAlignment(EVRTA_TextCenter);
			LabelComponent->SetWorldSize(LabelWorldSize);
			LabelComponent->RegisterComponent();
		}
	};

	CreateArrow(MovementArrowComponent, MovementLabelComponent, TEXT("DirectionDebug_MoveArrow"), TEXT("DirectionDebug_MoveLabel"), TEXT("Move"), MovementDirectionColor);
	CreateArrow(ActorArrowComponent, ActorLabelComponent, TEXT("DirectionDebug_ActorArrow"), TEXT("DirectionDebug_ActorLabel"), TEXT("Actor"), ActorDirectionColor);
	CreateArrow(ControllerArrowComponent, ControllerLabelComponent, TEXT("DirectionDebug_ControllerArrow"), TEXT("DirectionDebug_ControllerLabel"), TEXT("Controller"), ControllerDirectionColor);
}

void UDark_TdoreDirectionDebugComponent::DestroyArrowComponents()
{
	auto DestroyArrow = [](auto& ArrowComponent)
	{
		if (ArrowComponent)
		{
			ArrowComponent->DestroyComponent();
			ArrowComponent = nullptr;
		}
	};

	DestroyArrow(MovementArrowComponent);
	DestroyArrow(ActorArrowComponent);
	DestroyArrow(ControllerArrowComponent);
	DestroyArrow(MovementLabelComponent);
	DestroyArrow(ActorLabelComponent);
	DestroyArrow(ControllerLabelComponent);
}

bool UDark_TdoreDirectionDebugComponent::GetMovementDirection(FVector& OutDirection) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}

	FVector Direction = FVector::ZeroVector;
	if (const UMovementComponent* MovementComponent = OwnerPawn->GetMovementComponent())
	{
		if (const UCharacterMovementComponent* CharacterMovementComponent = Cast<UCharacterMovementComponent>(MovementComponent))
		{
			Direction = CharacterMovementComponent->GetCurrentAcceleration();
		}
		if (Direction.SizeSquared2D() <= KINDA_SMALL_NUMBER)
		{
			Direction = OwnerPawn->GetLastMovementInputVector();
		}
		if (bUseVelocityWhenNoInput && Direction.SizeSquared2D() <= KINDA_SMALL_NUMBER)
		{
			Direction = MovementComponent->Velocity;
		}
	}
	else
	{
		Direction = OwnerPawn->GetLastMovementInputVector();
	}

	Direction.Z = 0.0f;
	if (!Direction.Normalize())
	{
		return false;
	}

	OutDirection = Direction;
	return true;
}

bool UDark_TdoreDirectionDebugComponent::GetActorDirection(FVector& OutDirection) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	FVector Direction = OwnerActor->GetActorForwardVector();
	Direction.Z = 0.0f;
	if (!Direction.Normalize())
	{
		return false;
	}

	OutDirection = Direction;
	return true;
}

bool UDark_TdoreDirectionDebugComponent::GetControllerDirection(FVector& OutDirection) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}

	const FRotator ControlRotation = OwnerPawn->GetControlRotation();
	FVector Direction = FRotationMatrix(FRotator(0.0f, ControlRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X);
	Direction.Z = 0.0f;
	if (!Direction.Normalize())
	{
		return false;
	}

	OutDirection = Direction;
	return true;
}

void UDark_TdoreDirectionDebugComponent::DrawDirectionArrow(const FVector& Origin, const FVector& Direction, const FColor& Color, float VerticalOffset, const FString& Label) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector DrawOrigin = Origin + FVector(0.0f, 0.0f, VerticalOffset);
	const FVector DrawEnd = DrawOrigin + Direction * ArrowLength;
	DrawDebugDirectionalArrow(World, DrawOrigin, DrawEnd, ArrowHeadSize, Color, false, 0.0f, 0, LineThickness);

	if (bDrawLabels)
	{
		DrawDebugString(World, DrawEnd + FVector(0.0f, 0.0f, 12.0f), Label, nullptr, Color, 0.0f, true);
	}
}

void UDark_TdoreDirectionDebugComponent::UpdateArrowComponent(UArrowComponent* ArrowComponent, UTextRenderComponent* LabelComponent, const FVector& Origin, const FVector& Direction, const FColor& Color, float VerticalOffset, const FString& Label, bool bVisible) const
{
	if (!ArrowComponent)
	{
		return;
	}

	ArrowComponent->SetVisibility(bVisible);
	ArrowComponent->SetHiddenInGame(!bVisible);
	if (LabelComponent)
	{
		LabelComponent->SetVisibility(bVisible && bDrawLabels);
		LabelComponent->SetHiddenInGame(!bVisible || !bDrawLabels);
	}

	if (!bVisible)
	{
		return;
	}

	ArrowComponent->ArrowColor = Color;
	ArrowComponent->ArrowLength = ArrowLength;
	ArrowComponent->SetWorldLocation(Origin + FVector(0.0f, 0.0f, VerticalOffset));
	ArrowComponent->SetWorldRotation(Direction.Rotation());

	if (LabelComponent && bDrawLabels)
	{
		UWorld* World = GetWorld();
		const FVector LabelLocation = Origin + Direction * (ArrowLength + 18.0f) + FVector(0.0f, 0.0f, VerticalOffset + 28.0f);
		LabelComponent->SetText(FText::FromString(Label));
		LabelComponent->SetTextRenderColor(Color);
		LabelComponent->SetWorldSize(LabelWorldSize);
		LabelComponent->SetWorldLocation(LabelLocation);

		if (World)
		{
			if (const APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				FVector CameraLocation = FVector::ZeroVector;
				FRotator CameraRotation = FRotator::ZeroRotator;
				PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
				LabelComponent->SetWorldRotation((CameraLocation - LabelLocation).Rotation());
			}
		}
	}
}
