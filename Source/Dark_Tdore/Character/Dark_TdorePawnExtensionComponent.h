// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"

#include "Dark_TdorePawnExtensionComponent.generated.h"

namespace EEndPlayReason { enum Type : int; }

class UGameFrameworkComponentManager;
class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreAbilityTagRelationshipMapping;
struct FActorInitStateChangedParams;
struct FGameplayTag;

/**
 * UDark_TdorePawnExtensionComponent — Pawn 初始化协调器（参考 Lyra ULyraPawnExtensionComponent）
 *
 * 职责：
 *  - 协调 Pawn 上所有组件的 InitState 初始化顺序
 *  - 管理 ASC（AbilitySystemComponent）的关联/解除
 *  - 响应 Controller 变化
 *  - 存储 PawnData（DataAsset 驱动配置）
 */
UCLASS(MinimalAPI)
class UDark_TdorePawnExtensionComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:

	UDark_TdorePawnExtensionComponent(const FObjectInitializer& ObjectInitializer);

	/** 本特征名称，其他组件通过此名称声明依赖关系 */
	static const FName NAME_ActorFeatureName;

	//~ Begin IGameFrameworkInitStateInterface interface
	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	virtual void CheckDefaultInitialization() override;
	//~ End IGameFrameworkInitStateInterface interface

	/** 在指定 Actor 上查找 PawnExtensionComponent */
	UFUNCTION(BlueprintPure, Category = "DarkTdore|Pawn")
	static UDark_TdorePawnExtensionComponent* FindPawnExtensionComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<UDark_TdorePawnExtensionComponent>() : nullptr); }

	/** 获取能力系统组件 */
	UFUNCTION(BlueprintPure, Category = "DarkTdore|Pawn")
	UDark_TdoreAbilitySystemComponent* GetDark_TdoreAbilitySystemComponent() const { return AbilitySystemComponent; }

	/** 初始化 ASC，将 Pawn 设为 Avatar */
	void InitializeAbilitySystem(UDark_TdoreAbilitySystemComponent* InASC, AActor* InOwnerActor);

	/** 设置 TagRelationshipMapping — 在 PawnData 中配置后，初始化 ASC 时调用此方法 */
	void SetTagRelationshipMapping(UDark_TdoreAbilityTagRelationshipMapping* NewMapping);

	/** 解除 ASC 的 Avatar 关联 */
	void UninitializeAbilitySystem();

	/** Pawn 的 Controller 变化时调用 */
	void HandleControllerChanged();

	/** Pawn 的 PlayerState 复制完成时调用 */
	void HandlePlayerStateReplicated();

	/** Pawn 的 InputComponent 设置完成后调用 → 通知 HeroComponent 绑定输入 */
	void SetupPlayerInputComponent();

	/** 注册 ASC 初始化回调 + 如果已初始化则立即广播 */
	void OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate Delegate);

	/** 注册 ASC 解除初始化回调 */
	void OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate Delegate);

protected:

	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** ASC 初始化完成委托（其他组件注册此委托，初始化自己的子系统） */
	FSimpleMulticastDelegate OnAbilitySystemInitialized;

	/** ASC 解除初始化委托 */
	FSimpleMulticastDelegate OnAbilitySystemUninitialized;

	/** 缓存的能力系统组件指针 */
	UPROPERTY()
	TObjectPtr<UDark_TdoreAbilitySystemComponent> AbilitySystemComponent;
};
