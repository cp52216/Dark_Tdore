// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModularCharacter.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"
#include "Dark_TdoreCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreCameraComponent;
class UDark_TdoreCameraMode;
class UDark_TdoreHealthComponent;
class UDark_TdoreInputConfig;
class UDark_TdorePawnExtensionComponent;
class UDark_TdoreHeroComponent;
class ADark_TdorePlayerState;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 * ADark_TdoreCharacter — 玩家角色（参考 Lyra ALyraCharacter）
 *
 * 继承: AModularCharacter → ACharacter
 *
 * ASC 在哪里？→ PlayerState 上（非 Character）
 * 为什么？
 *  1. 持久性：Character 死亡/重生时 PlayerState 不销毁，属性不丢失
 *  2. Owner/Avatar 分离：OwnerActor = PlayerState, AvatarActor = Character
 *  3. 技能授权：AbilitySet 通过 PlayerState 一次性授予，所有控制的 Pawn 继承
 *
 * Character 只负责底层功能：Camera + Movement + Do*** 方法
 * 输入绑定由 HeroComponent（参考 Lyra ULyraHeroComponent）处理
 */
UCLASS(abstract)
class ADark_TdoreCharacter : public AModularCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

	// ============ 组件 ============

	/** 摄像机组件 — 管理摄像机模式栈和混合过渡（参考 Lyra ULyraCameraComponent） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UDark_TdoreCameraComponent* CameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UDark_TdorePawnExtensionComponent* PawnExtComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UDark_TdoreHeroComponent* HeroComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UDark_TdoreHealthComponent* HealthComponent;

public:
	ADark_TdoreCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ASC 委托到 PlayerState（OwnerActor = PlayerState, AvatarActor = this）
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintCallable, Category="DarkTdore|Character")
	ADark_TdorePlayerState* GetDark_TdorePlayerState() const;

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** PlayerState.ASC 就绪后回调（由 PawnExtComp 的 OnAbilitySystemInitialized 委托触发） */
	void OnAbilitySystemInitialized();

	/** PlayerState.ASC 解除后回调 */
	void OnAbilitySystemUninitialized();

public:
	// 参考 Lyra ALyraCharacter：底层方法，由 HeroComponent 调用
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	FORCEINLINE class UDark_TdoreCameraComponent* GetDarkTdoreCameraComponent() const { return CameraComponent; }
};
