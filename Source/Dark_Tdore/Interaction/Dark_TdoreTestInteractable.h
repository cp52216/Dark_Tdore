// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dark_TdoreInteractableTarget.h"
#include "Dark_TdoreTestInteractable.generated.h"

class USphereComponent;

UCLASS()
class ADark_TdoreTestInteractable : public AActor, public IDark_TdoreInteractableTarget
{
	GENERATED_BODY()

public:
	ADark_TdoreTestInteractable();

	virtual void GatherInteractionOptions(const FDark_TdoreInteractionQuery& InteractQuery, FInteractionOptionBuilder& OptionBuilder) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Interaction)
	FText InteractionText = FText::FromString(TEXT("\u62FE\u53D6\u6D4B\u8BD5\u7269\u54C1"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Interaction)
	TSubclassOf<UGameplayAbility> InteractionAbilityClass;

protected:
	/** 碰撞球体 — 让 Overlap 能检测到此 Actor（必须 ECC_WorldDynamic） */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> SphereCollision;
};
