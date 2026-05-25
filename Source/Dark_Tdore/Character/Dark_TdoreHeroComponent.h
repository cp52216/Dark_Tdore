// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Dark_TdoreHeroComponent.generated.h"

class UInputComponent;
class UDark_TdoreCameraMode;
class UDark_TdoreInputConfig;
struct FInputActionValue;

/**
 * UDark_TdoreHeroComponent — 玩家输入组件（参考 Lyra ULyraHeroComponent）
 *
 * 实现 IGameFrameworkInitStateInterface，通过 InitState 状态机自动绑定输入：
 *  Spawned → DataAvailable → DataInitialized（绑定输入+摄像机） → GameplayReady
 *
 * 摄像机模式管理：
 *  - 绑定 CameraComponent 的 DetermineCameraModeDelegate
 *  - 每帧从 PawnData.DefaultCameraMode 或 AbilityCameraMode 返回当前模式
 *  - 技能可通过 SetAbilityCameraMode 临时覆盖（如瞄准、大招视角）
 *
 * Character 只提供底层 DoMove/DoLook/DoJump 方法。
 * HeroComponent 负责 EnhancedInput 绑定 + 技能输入路由 + 摄像机模式选择。
 */
UCLASS(MinimalAPI, Blueprintable, Meta=(BlueprintSpawnableComponent))
class UDark_TdoreHeroComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:
	UDark_TdoreHeroComponent(const FObjectInitializer& ObjectInitializer);

	/** InitState 特征名 */
	static const FName NAME_ActorFeatureName;

	//~ Begin IGameFrameworkInitStateInterface
	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	virtual void CheckDefaultInitialization() override;
	//~ End IGameFrameworkInitStateInterface

	// ============ 摄像机模式覆盖（GA 调用） ============

	/** 技能激活时覆盖摄像机模式 */
	void SetAbilityCameraMode(TSubclassOf<UDark_TdoreCameraMode> CameraMode, const FGameplayAbilitySpecHandle& OwningSpecHandle);

	/** 技能结束时清除摄像机覆盖 */
	void ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle);

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 执行输入绑定（由 HandleChangeInitState 在 DataInitialized 时调用） */
	void InitializePlayerInput();

	/** 确定当前应该使用的摄像机模式 */
	TSubclassOf<UDark_TdoreCameraMode> DetermineCameraMode() const;

	// ---- 输入回调 ----
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Jump();
	void Input_JumpEnd();
	void Input_AbilityTagPressed(FGameplayTag InputTag);
	void Input_AbilityTagReleased(FGameplayTag InputTag);

private:
	/** 是否已绑定输入（避免重复绑定） */
	bool bReadyToBindInputs = false;

	/** 技能覆盖的摄像机模式 */
	UPROPERTY()
	TSubclassOf<UDark_TdoreCameraMode> AbilityCameraMode;

	/** 最后设置摄像机模式的技能句柄 */
	FGameplayAbilitySpecHandle AbilityCameraModeOwningSpecHandle;
};
