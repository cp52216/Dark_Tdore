// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreHeroComponent.h"

#include "Dark_TdoreCharacter.h"
#include "Dark_Tdore.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "Camera/Dark_TdoreCameraComponent.h"
#include "Camera/Dark_TdoreCameraMode.h"
#include "Character/Dark_TdorePawnExtensionComponent.h"
#include "Character/Dark_TdorePawnData.h"
#include "Components/GameFrameworkComponentManager.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "Input/Dark_TdoreInputConfig.h"
#include "Player/Dark_TdorePlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreHeroComponent)

const FName UDark_TdoreHeroComponent::NAME_ActorFeatureName("Hero");

UDark_TdoreHeroComponent::UDark_TdoreHeroComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ============ InitState 接口 ============

// 参考 Lyra HeroComponent：注册到 InitState 系统
void UDark_TdoreHeroComponent::OnRegister()
{
	Super::OnRegister();
	RegisterInitStateFeature();
}

void UDark_TdoreHeroComponent::BeginPlay()
{
	Super::BeginPlay();

	// 监听 PawnExtComponent 的 InitState 变化
	BindOnActorInitStateChanged(UDark_TdorePawnExtensionComponent::NAME_ActorFeatureName, FGameplayTag(), false);

	// 推进到 Spawned，然后尝试继续
	ensure(TryToChangeInitState(FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned"))));
	CheckDefaultInitialization();
}

void UDark_TdoreHeroComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterInitStateFeature();
	Super::EndPlay(EndPlayReason);
}

// 参考 Lyra HeroComponent：依赖 PawnExtComp 已到达 DataInitialized
bool UDark_TdoreHeroComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
	check(Manager);

	APawn* Pawn = GetPawn<APawn>();
	static const FGameplayTag Spawned = FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned"));
	static const FGameplayTag DataAvailable = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable"));
	static const FGameplayTag DataInitialized = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized"));
	static const FGameplayTag GameplayReady = FGameplayTag::RequestGameplayTag(TEXT("InitState.GameplayReady"));

	if (!CurrentState.IsValid() && DesiredState == Spawned)
	{
		return Pawn != nullptr;
	}
	if (CurrentState == Spawned && DesiredState == DataAvailable)
	{
		// 需要 PlayerState 和 PawnData
		if (!GetController<APlayerController>()) return false;

		const ADark_TdorePlayerState* PS = Pawn ? Pawn->GetPlayerState<ADark_TdorePlayerState>() : nullptr;
		if (!PS || !PS->GetPawnData()) return false;

		return true;
	}
	if (CurrentState == DataAvailable && DesiredState == DataInitialized)
	{
		// 需要 PawnExtComp 到达 DataInitialized + InputComponent 已创建
		return Manager->HasFeatureReachedInitState(Pawn, UDark_TdorePawnExtensionComponent::NAME_ActorFeatureName, DataInitialized)
			&& Pawn->InputComponent != nullptr;
	}
	if (CurrentState == DataInitialized && DesiredState == GameplayReady)
	{
		return true;
	}

	return false;
}

// 参考 Lyra HeroComponent：推进到 DataInitialized 时绑定输入
void UDark_TdoreHeroComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	static const FGameplayTag DataInitialized = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized"));

	if (DesiredState == DataInitialized)
	{
		InitializePlayerInput();
	}
}

void UDark_TdoreHeroComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName != NAME_ActorFeatureName)
	{
		CheckDefaultInitialization();
	}
}

void UDark_TdoreHeroComponent::CheckDefaultInitialization()
{
	static const TArray<FGameplayTag> StateChain = {
		FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned")),
		FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable")),
		FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized")),
		FGameplayTag::RequestGameplayTag(TEXT("InitState.GameplayReady"))
	};

	ContinueInitStateChain(StateChain);
}

// ============ 输入绑定 ============

// 参考 Lyra ULyraHeroComponent::InitializePlayerInput
// 在 InitState 到达 DataInitialized 时自动调用
void UDark_TdoreHeroComponent::InitializePlayerInput()
{
	if (bReadyToBindInputs) return;
	bReadyToBindInputs = true;

	const APawn* Pawn = GetPawn<APawn>();
	const ADark_TdorePlayerState* PS = Pawn ? Pawn->GetPlayerState<ADark_TdorePlayerState>() : nullptr;
	const UDark_TdorePawnData* PawnData = PS ? PS->GetPawnData() : nullptr;
	if (!PawnData || !PawnData->InputConfig) return;

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!EnhancedInputComponent) return;

	const UDark_TdoreInputConfig* InputConfig = PawnData->InputConfig;

	// ---- Native 输入 ----
	if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.Jump")), false))
	{
		EnhancedInputComponent->BindAction(IA, ETriggerEvent::Started, this, &UDark_TdoreHeroComponent::Input_Jump);
		EnhancedInputComponent->BindAction(IA, ETriggerEvent::Completed, this, &UDark_TdoreHeroComponent::Input_JumpEnd);
	}
	if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.Move")), false))
	{
		EnhancedInputComponent->BindAction(IA, ETriggerEvent::Triggered, this, &UDark_TdoreHeroComponent::Input_Move);
	}
	if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.LookMouse")), false))
	{
		EnhancedInputComponent->BindAction(IA, ETriggerEvent::Triggered, this, &UDark_TdoreHeroComponent::Input_Look);
	}
	if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.Look")), false))
	{
		EnhancedInputComponent->BindAction(IA, ETriggerEvent::Triggered, this, &UDark_TdoreHeroComponent::Input_Look);
	}

	// ---- 技能输入 ----
	for (const FDark_TdoreInputAction& Action : InputConfig->AbilityInputActions)
	{
		if (Action.InputAction && Action.InputTag.IsValid())
		{
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Triggered, this,
				&UDark_TdoreHeroComponent::Input_AbilityTagPressed, Action.InputTag);
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, this,
				&UDark_TdoreHeroComponent::Input_AbilityTagReleased, Action.InputTag);
		}
	}

	// ---- 摄像机模式绑定 ----
	if (const ADark_TdoreCharacter* Character = Cast<ADark_TdoreCharacter>(Pawn))
	{
		if (UDark_TdoreCameraComponent* CamComp = Character->GetDarkTdoreCameraComponent())
		{
			CamComp->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode);
		}
	}
}

// ============ 输入回调 ============

void UDark_TdoreHeroComponent::Input_Move(const FInputActionValue& Value)
{
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;

	if (Controller)
	{
		const FVector2D MovementVector = Value.Get<FVector2D>();
		const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

		if (MovementVector.X != 0.0f)
			Pawn->AddMovementInput(MovementRotation.RotateVector(FVector::RightVector), MovementVector.X);
		if (MovementVector.Y != 0.0f)
			Pawn->AddMovementInput(MovementRotation.RotateVector(FVector::ForwardVector), MovementVector.Y);
	}
}

void UDark_TdoreHeroComponent::Input_Look(const FInputActionValue& Value)
{
	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn) return;

	const FVector2D LookVector = Value.Get<FVector2D>();
	if (LookVector.X != 0.0f) Pawn->AddControllerYawInput(LookVector.X);
	if (LookVector.Y != 0.0f) Pawn->AddControllerPitchInput(LookVector.Y);
}

void UDark_TdoreHeroComponent::Input_Jump()
{
	if (ADark_TdoreCharacter* Character = Cast<ADark_TdoreCharacter>(GetPawn<APawn>()))
		Character->DoJumpStart();
}

void UDark_TdoreHeroComponent::Input_JumpEnd()
{
	if (ADark_TdoreCharacter* Character = Cast<ADark_TdoreCharacter>(GetPawn<APawn>()))
		Character->DoJumpEnd();
}

void UDark_TdoreHeroComponent::Input_AbilityTagPressed(FGameplayTag InputTag)
{
	if (IAbilitySystemInterface* ASCInterface = Cast<IAbilitySystemInterface>(GetPawn<APawn>()))
		if (UDark_TdoreAbilitySystemComponent* ASC = Cast<UDark_TdoreAbilitySystemComponent>(ASCInterface->GetAbilitySystemComponent()))
			ASC->AbilityInputTagPressed(InputTag);
}

void UDark_TdoreHeroComponent::Input_AbilityTagReleased(FGameplayTag InputTag)
{
	if (IAbilitySystemInterface* ASCInterface = Cast<IAbilitySystemInterface>(GetPawn<APawn>()))
		if (UDark_TdoreAbilitySystemComponent* ASC = Cast<UDark_TdoreAbilitySystemComponent>(ASCInterface->GetAbilitySystemComponent()))
			ASC->AbilityInputTagReleased(InputTag);
}

// ============ 摄像机模式 ============

TSubclassOf<UDark_TdoreCameraMode> UDark_TdoreHeroComponent::DetermineCameraMode() const
{
	// 技能覆盖优先
	if (AbilityCameraMode)
	{
		return AbilityCameraMode;
	}

	// 默认使用 PawnData 中的 DefaultCameraMode
	const APawn* Pawn = GetPawn<APawn>();
	const ADark_TdorePlayerState* PS = Pawn ? Pawn->GetPlayerState<ADark_TdorePlayerState>() : nullptr;
	const UDark_TdorePawnData* PawnData = PS ? PS->GetPawnData() : nullptr;

	if (PawnData && PawnData->DefaultCameraMode)
	{
		return PawnData->DefaultCameraMode;
	}

	return nullptr;
}

void UDark_TdoreHeroComponent::SetAbilityCameraMode(TSubclassOf<UDark_TdoreCameraMode> CameraMode, const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (CameraMode)
	{
		AbilityCameraMode = CameraMode;
		AbilityCameraModeOwningSpecHandle = OwningSpecHandle;
	}
}

void UDark_TdoreHeroComponent::ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (AbilityCameraModeOwningSpecHandle == OwningSpecHandle)
	{
		AbilityCameraMode = nullptr;
		AbilityCameraModeOwningSpecHandle = FGameplayAbilitySpecHandle();
	}
}
