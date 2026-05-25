// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AttributeSet.h"

#include "Dark_TdoreAttributeSet.generated.h"

class UDark_TdoreAbilitySystemComponent;
struct FGameplayEffectSpec;

// ========================================================================
// ATTRIBUTE_ACCESSORS — 属性访问器宏（参考 Lyra）
//   为属性自动生成 static FGameplayAttribute Get<Name>Attribute()
//   以及 Get<Name> / Set<Name> / Init<Name> 实例方法
// ========================================================================
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

// ========================================================================
// FDarkTdoreAttributeEvent — 属性变化委托（参考 Lyra FLyraAttributeEvent）
//   参数: Instigator, Causer, EffectSpec, Magnitude, OldValue, NewValue
//   客户端上的某些参数可能为 null（网络同步限制）
// ========================================================================
DECLARE_MULTICAST_DELEGATE_SixParams(FDarkTdoreAttributeEvent,
	AActor* /*Instigator*/, AActor* /*Causer*/,
	const FGameplayEffectSpec* /*EffectSpec*/, float /*Magnitude*/,
	float /*OldValue*/, float /*NewValue*/);

/**
 * UDark_TdoreAttributeSet — 属性集基类（参考 Lyra ULyraAttributeSet）
 *
 * 所有属性集的公共基类。提供：
 *   - ATTRIBUTE_ACCESSORS 宏
 *   - FDarkTdoreAttributeEvent 委托类型
 *   - GetWorld() / GetDarkTdoreAbilitySystemComponent() 便捷方法
 *
 * 子类：UDark_TdoreHealthSet（生命值管线）、UDark_TdoreCombatSet（战斗数值）
 */
UCLASS(MinimalAPI)
class UDark_TdoreAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UDark_TdoreAttributeSet();

	UWorld* GetWorld() const override;

	UDark_TdoreAbilitySystemComponent* GetDarkTdoreAbilitySystemComponent() const;
};
