// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "Dark_TdoreAttributeSet.h"
#include "NativeGameplayTags.h"

#include "Dark_TdoreHealthSet.generated.h"

struct FGameplayEffectModCallbackData;

/** 伤害免疫 Tag：拥有此 Tag 时免疫非自毁伤害 */
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_DamageImmunity);

/** 自毁伤害 Tag：无视免疫的直接伤害 */
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_DamageSelfDestruct);

/** 伤害消息 Verb：通过 GameplayMessageSubsystem 广播供其他系统订阅 */
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_Damage_Message);

/**
 * UDark_TdoreHealthSet — 生命值属性集（参考 Lyra ULyraHealthSet）
 *
 * 管理 Health / MaxHealth / Damage / Healing 四个属性的完整管线：
 *
 *   ┌─ GE 写入 Damage(50) ─┐
 *   │                       ▼
 *   │  PreGameplayEffectExecute: 检查 DamageImmunity/GodMode → 可能归零
 *   │                       ▼
 *   │  PostGameplayEffectExecute:
 *   │    Damage → SetHealth(Health - Damage) → SetDamage(0)
 *   │    Healing → SetHealth(Health + Healing) → SetHealing(0)
 *   │                       ▼
 *   │  OnHealthChanged.Broadcast ← HealthComponent 监听 → UI 更新
 *   │                       ▼
 *   │  Health <= 0 → OnOutOfHealth.Broadcast → HealthComponent.HandleOutOfHealth
 *   │                       ▼
 *   │  bOutOfHealth = true → 下次不再重复广播
 *
 * PostAttributeChange: MaxHealth 变化时 → 若当前 Health 超过新上限则 Clamp 到新值
 * PreAttributeChange/PreAttributeBaseChange: Clamp Health[0, MaxHealth], MaxHealth[1, ∞)
 */
UCLASS(MinimalAPI, BlueprintType)
class UDark_TdoreHealthSet : public UDark_TdoreAttributeSet
{
	GENERATED_BODY()

public:
	UDark_TdoreHealthSet();

	ATTRIBUTE_ACCESSORS(UDark_TdoreHealthSet, Health);
	ATTRIBUTE_ACCESSORS(UDark_TdoreHealthSet, MaxHealth);
	ATTRIBUTE_ACCESSORS(UDark_TdoreHealthSet, Healing);
	ATTRIBUTE_ACCESSORS(UDark_TdoreHealthSet, Damage);

	/** 生命值变化委托 */
	mutable FDarkTdoreAttributeEvent OnHealthChanged;

	/** 最大生命值变化委托 */
	mutable FDarkTdoreAttributeEvent OnMaxHealthChanged;

	/** 生命值归零委托（触发死亡流程） */
	mutable FDarkTdoreAttributeEvent OnOutOfHealth;

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	/** GE 执行前：检查伤害免疫，保存当前值 */
	virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;

	/** GE 执行后：Meta 属性转换到 Health，广播变化委托 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	/** 属性基础值变化前 Clamp */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 属性当前值变化前 Clamp + 保存旧值 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 属性变化后：MaxHealth 降低时 Clamp Health */
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

private:
	// ========== 状态属性 ==========

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "DarkTdore|Health", Meta = (HideFromModifiers, AllowPrivateAccess = true))
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "DarkTdore|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxHealth;

	bool bOutOfHealth = false;

	float MaxHealthBeforeAttributeChange = 0.0f;
	float HealthBeforeAttributeChange = 0.0f;

	// ========== Meta 属性（临时管道值，不持久化） ==========

	UPROPERTY(BlueprintReadOnly, Category = "DarkTdore|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Healing;

	UPROPERTY(BlueprintReadOnly, Category = "DarkTdore|Health", Meta = (HideFromModifiers, AllowPrivateAccess = true))
	FGameplayAttributeData Damage;
};
