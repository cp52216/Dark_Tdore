// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dark_TdoreDirectionDebugComponent.generated.h"

class UMovementComponent;
class UArrowComponent;
class UTextRenderComponent;

/**
 * Draws ground arrows for movement, actor-facing, and controller-facing directions.
 *
 * Drop this on any Pawn/Character while tuning combat movement and facing rules.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(Debug), meta=(BlueprintSpawnableComponent))
class UDark_TdoreDirectionDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreDirectionDebugComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Direction Debug")
	void SetDebugDrawEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Direction Debug")
	bool IsDebugDrawEnabled() const { return bDrawDebug; }

protected:
	virtual void BeginPlay() override;

private:
	FVector GetArrowOrigin() const;
	void EnsureArrowComponents();
	void DestroyArrowComponents();
	bool GetMovementDirection(FVector& OutDirection) const;
	bool GetActorDirection(FVector& OutDirection) const;
	bool GetControllerDirection(FVector& OutDirection) const;
	void DrawDirectionArrow(const FVector& Origin, const FVector& Direction, const FColor& Color, float VerticalOffset, const FString& Label) const;
	void UpdateArrowComponent(UArrowComponent* ArrowComponent, UTextRenderComponent* LabelComponent, const FVector& Origin, const FVector& Direction, const FColor& Color, float VerticalOffset, const FString& Label, bool bVisible) const;

private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawDebug = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawOnlyForLocallyControlledPawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	bool bUseVelocityWhenNoInput = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	bool bUseArrowComponents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float ArrowLength = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float ArrowHeadSize = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	float GroundOffset = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	float LineThickness = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float LabelWorldSize = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	FColor MovementDirectionColor = FColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	FColor ActorDirectionColor = FColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction Debug", meta = (AllowPrivateAccess = "true"))
	FColor ControllerDirectionColor = FColor::Red;

	UPROPERTY(Transient)
	TObjectPtr<UArrowComponent> MovementArrowComponent;

	UPROPERTY(Transient)
	TObjectPtr<UArrowComponent> ActorArrowComponent;

	UPROPERTY(Transient)
	TObjectPtr<UArrowComponent> ControllerArrowComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> MovementLabelComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> ActorLabelComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> ControllerLabelComponent;
};
