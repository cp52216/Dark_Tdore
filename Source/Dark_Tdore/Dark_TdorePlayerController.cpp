// Copyright Epic Games, Inc. All Rights Reserved.


#include "Dark_TdorePlayerController.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Dark_Tdore.h"
#include "Widgets/Input/SVirtualJoystick.h"

void ADark_TdorePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogDark_Tdore, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void ADark_TdorePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

// 参考 Lyra ALyraPlayerController::PostProcessInput()
// 在所有输入处理完毕后，处理技能输入缓冲（Pressed/Released 队列）
// ASC 在 PlayerState 上，通过 IAbilitySystemInterface 获取
void ADark_TdorePlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	if (IAbilitySystemInterface* ASCInterface = Cast<IAbilitySystemInterface>(GetPawn()))
	{
		if (UDark_TdoreAbilitySystemComponent* ASC = Cast<UDark_TdoreAbilitySystemComponent>(ASCInterface->GetAbilitySystemComponent()))
		{
			ASC->ProcessAbilityInput(DeltaTime, bGamePaused);
		}
	}

	Super::PostProcessInput(DeltaTime, bGamePaused);
}

bool ADark_TdorePlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
