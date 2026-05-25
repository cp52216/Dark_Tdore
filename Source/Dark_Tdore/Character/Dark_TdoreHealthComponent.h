// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameFrameworkComponent.h"
#include "Dark_TdoreHealthComponent.generated.h"

class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreHealthSet;
struct FGameplayEffectSpec;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDarkTdoreHealth_DeathEvent, AActor*, OwningActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FDarkTdoreHealth_AttributeChanged, class UDark_TdoreHealthComponent*, HealthComponent, float, OldValue, float, NewValue, AActor*, Instigator);

/**
 * EDeathState — 死亡状态（参考 Lyra ELyraDeathState）
 */
UENUM(BlueprintType)
enum class EDeathState : uint8
{
	NotDead = 0,
	DeathStarted,
	DeathFinished
};

/**
 * UDark_TdoreHealthComponent — 血量管理组件（参考 Lyra ULyraHealthComponent）
 *
 * 职责：
 *  - 初始化时绑定 ASC 的 AttributeSet 代理
 *  - 监听 Health / MaxHealth / OutOfHealth 变化
 *  - 管理死亡流程：StartDeath → FinishDeath
 *  - 设置 Status.Death.Dying / Status.Death.Dead 标签
 */
UCLASS(MinimalAPI, Blueprintable, Meta=(BlueprintSpawnableComponent))
class UDark_TdoreHealthComponent : public UGameFrameworkComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreHealthComponent(const FObjectInitializer& ObjectInitializer);

	/** 在指定 Actor 上查找 HealthComponent */
	UFUNCTION(BlueprintPure, Category = "DarkTdore|Health")
	static UDark_TdoreHealthComponent* FindHealthComponent(const AActor* Actor) { return Actor ? Actor->FindComponentByClass<UDark_TdoreHealthComponent>() : nullptr; }

	/** 绑定 ASC，注册属性变化监听 */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Health")
	void InitializeWithAbilitySystem(UDark_TdoreAbilitySystemComponent* InASC);

	/** 解除 ASC 绑定 */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Health")
	void UninitializeFromAbilitySystem();

	// ---- 属性查询 ----
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Health")
	float GetHealth() const;

	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Health")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Health")
	float GetHealthNormalized() const;

	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Health")
	EDeathState GetDeathState() const { return DeathState; }

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "DarkTdore|Health", Meta = (ExpandBoolAsExecs = "ReturnValue"))
	bool IsDeadOrDying() const { return DeathState > EDeathState::NotDead; }

	// ---- 死亡流程 ----
	virtual void StartDeath();
	virtual void FinishDeath();

public:
	/** 生命值变化委托 */
	UPROPERTY(BlueprintAssignable)
	FDarkTdoreHealth_AttributeChanged OnHealthChanged;

	/** 最大生命值变化委托 */
	UPROPERTY(BlueprintAssignable)
	FDarkTdoreHealth_AttributeChanged OnMaxHealthChanged;

	/** 死亡开始委托 */
	UPROPERTY(BlueprintAssignable)
	FDarkTdoreHealth_DeathEvent OnDeathStarted;

	/** 死亡结束委托 */
	UPROPERTY(BlueprintAssignable)
	FDarkTdoreHealth_DeathEvent OnDeathFinished;

protected:
	virtual void OnUnregister() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void ClearGameplayTags();

	void HandleHealthChanged(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue);
	void HandleMaxHealthChanged(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue);
	void HandleOutOfHealth(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue);

	UFUNCTION()
	void OnRep_DeathState(EDeathState OldDeathState);

protected:
	UPROPERTY()
	TObjectPtr<UDark_TdoreAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<const UDark_TdoreHealthSet> HealthSet;

	UPROPERTY(ReplicatedUsing = OnRep_DeathState)
	EDeathState DeathState = EDeathState::NotDead;
};
