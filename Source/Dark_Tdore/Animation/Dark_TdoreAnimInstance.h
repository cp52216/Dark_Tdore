// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"
#include "Dark_TdoreAnimInstance.generated.h"

class UAbilitySystemComponent;

/**
 * UDark_TdoreAnimInstance — 动画蓝图 C++ 基类（参考 Lyra ULyraAnimInstance）
 *
 * 核心设计理念：
 *   - C++ 只做数据桥接，动画逻辑全部在动画蓝图中实现
 *   - 通过 GameplayTagPropertyMap 将 GAS 标签自动映射为蓝图变量
 *     例如：角色有 Gameplay.Crouching 标签 → 蓝图变量 bIsCrouching = true
 *   - GroundDistance 供 FootIK 等后处理使用
 *   - 动画蓝图只需继承此类即可自动获得 GAS 状态同步
 *
 * 动画蓝图中的使用方式：
 *   1. 创建一个动画蓝图，父类选 UDark_TdoreAnimInstance
 *   2. 在 Class Defaults → GameplayTags 下添加 Tag 到蓝图变量的映射
 *   3. 利用事件图表检查 GroundDistance 做 FootIK
 */
UCLASS(Config = Game)
class UDark_TdoreAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UDark_TdoreAnimInstance(const FObjectInitializer& ObjectInitializer);

	/**
	 * 绑定 GameplayTagPropertyMap 到指定的 ASC
	 * 在 NativeInitializeAnimation 中自动调用（找到 OwningActor 的 ASC）
	 */
	virtual void InitializeWithAbilitySystem(UAbilitySystemComponent* ASC);

protected:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/**
	 * GameplayTag 到蓝图变量的自动映射表
	 *
	 * 在动画蓝图的 Class Defaults 中配置：
	 *   GameplayTags
	 *     ├── Gameplay.Crouching     →  bIsCrouching (bool)
	 *     ├── Gameplay.MovementStopped →  bMovementStopped (bool)
	 *     ├── Status.Death           →  bIsDead (bool)
	 *     └── ...
	 *
	 * 当角色 ASC 拥有/移除对应的 GameplayTag 时，
	 * 映射的蓝图变量会自动更新，无需手动轮询。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "GameplayTags")
	FGameplayTagBlueprintPropertyMap GameplayTagPropertyMap;

	/** 角色离地面的距离（每帧从 CharacterMovementComponent 获取） */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	float GroundDistance = -1.0f;
};
