// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreCharacter.h"

#include "Engine/LocalPlayer.h"
#include "Camera/Dark_TdoreCameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Character/Dark_TdoreCharacterMovementComponent.h"
#include "Character/Dark_TdorePawnExtensionComponent.h"
#include "Character/Dark_TdoreHealthComponent.h"
#include "Character/Dark_TdoreHeroComponent.h"
#include "Character/Dark_TdorePawnData.h"
#include "Input/Dark_TdoreInputConfig.h"
#include "Player/Dark_TdorePlayerState.h"

#include "Dark_Tdore.h"

static FName NAME_DarkTdoreCharacterCollisionProfile_Mesh(TEXT("DarkTdorePawnMesh"));

// ============ 构造 ============

ADark_TdoreCharacter::ADark_TdoreCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDark_TdoreCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 参考 Lyra：旋转网格体使其 X 轴朝前（UE 骨骼默认是 Y 轴朝前）
	USkeletalMeshComponent* MeshComp = GetMesh();
	check(MeshComp);
	MeshComp->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	UDark_TdoreCharacterMovementComponent* MoveComp = CastChecked<UDark_TdoreCharacterMovementComponent>(GetCharacterMovement());
	MoveComp->GravityScale = 1.0f;
	MoveComp->MaxAcceleration = 2400.0f;
	MoveComp->BrakingFrictionFactor = 1.0f;
	MoveComp->BrakingDecelerationWalking = 1400.0f;
	MoveComp->bOrientRotationToMovement = true;
	MoveComp->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	MoveComp->JumpZVelocity = 500.f;
	MoveComp->AirControl = 0.35f;
	MoveComp->MaxWalkSpeed = 500.f;
	MoveComp->MinAnalogWalkSpeed = 20.f;
	MoveComp->BrakingDecelerationFalling = 1500.0f;
	// 参考 Lyra：禁用动画 RootMotion 期间的物理旋转（由动画驱动旋转）
	MoveComp->bAllowPhysicsRotationDuringAnimRootMotion = false;

	// 摄像机组件 — Lyra 式的摄像机模式栈管理（替换传统 SpringArm+FollowCamera）
	CameraComponent = CreateDefaultSubobject<UDark_TdoreCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetupAttachment(RootComponent);
	CameraComponent->bUsePawnControlRotation = true;

	// PawnExtensionComponent — 初始化协调器（参考 Lyra）
	PawnExtComponent = CreateDefaultSubobject<UDark_TdorePawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	PawnExtComponent->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemInitialized));
	PawnExtComponent->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemUninitialized));

	// HeroComponent — 输入处理（参考 Lyra ULyraHeroComponent）
	HeroComponent = CreateDefaultSubobject<UDark_TdoreHeroComponent>(TEXT("HeroComponent"));

	// HealthComponent — 血量管理（参考 Lyra ULyraHealthComponent）
	HealthComponent = CreateDefaultSubobject<UDark_TdoreHealthComponent>(TEXT("HealthComponent"));
}

// ============ GAS 接口 ============

UAbilitySystemComponent* ADark_TdoreCharacter::GetAbilitySystemComponent() const
{
	if (!PawnExtComponent) return nullptr;
	return PawnExtComponent->GetDark_TdoreAbilitySystemComponent();
}

ADark_TdorePlayerState* ADark_TdoreCharacter::GetDark_TdorePlayerState() const
{
	return Cast<ADark_TdorePlayerState>(GetPlayerState());
}

// ============ 生命周期 ============

void ADark_TdoreCharacter::BeginPlay()
{
	Super::BeginPlay();
}

// 参考 Lyra：将 PlayerState.ASC 关联到此 Character，HeroComponent 通过 IAbilitySystemInterface 获取
void ADark_TdoreCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (ADark_TdorePlayerState* PS = GetDark_TdorePlayerState())
	{
		PawnExtComponent->InitializeAbilitySystem(PS->GetDark_TdoreAbilitySystemComponent(), PS);
	}

	PawnExtComponent->HandleControllerChanged();
}

void ADark_TdoreCharacter::UnPossessed()
{
	Super::UnPossessed();
	PawnExtComponent->HandleControllerChanged();
}

void ADark_TdoreCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	PawnExtComponent->HandlePlayerStateReplicated();
}

// 参考 Lyra ALyraCharacter::OnAbilitySystemInitialized
// ASC 就绪后初始化 HealthComponent
void ADark_TdoreCharacter::OnAbilitySystemInitialized()
{
	UE_LOG(LogDark_Tdore, Log, TEXT("Character [%s] ASC initialized via PlayerState"), *GetName());

	if (UDark_TdoreAbilitySystemComponent* ASC = Cast<UDark_TdoreAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		HealthComponent->InitializeWithAbilitySystem(ASC);
	}
}

void ADark_TdoreCharacter::OnAbilitySystemUninitialized()
{
}

// ============ 输入绑定 ============

// 参考 Lyra ALyraCharacter：委托给 HeroComponent
// InputConfig 通过 PlayerState→PawnData 获取，集中管理
// 参考 Lyra ALyraCharacter：委托给 PawnExtComp
// PawnExtComp 推进 InitState → HeroComponent 监听到 DataInitialized → 自动绑定输入
void ADark_TdoreCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	PawnExtComponent->SetupPlayerInputComponent();
}

// ============ 底层移动方法（由 HeroComponent 调用） ============

void ADark_TdoreCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void ADark_TdoreCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ADark_TdoreCharacter::DoJumpStart()
{
	Jump();
}

void ADark_TdoreCharacter::DoJumpEnd()
{
	StopJumping();
}
