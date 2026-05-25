// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Dark_TdorePlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UDark_TdoreAbilitySystemComponent;

/**
 * ADark_TdorePlayerController — 玩家控制器
 *
 * 参考 Lyra ALyraPlayerController::PostProcessInput：
 * ProcessAbilityInput 在 PostProcessInput 中调用，不在 Character Tick
 * 原因：PostProcessInput 在所有输入处理完成后执行，是处理技能输入的最佳时机
 */
UCLASS(abstract)
class ADark_TdorePlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** 技能输入处理（参考 Lyra：PostProcessInput 中调用 ProcessAbilityInput） */
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

};
