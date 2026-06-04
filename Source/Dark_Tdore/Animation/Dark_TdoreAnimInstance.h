// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"
#include "Dark_TdoreAnimInstance.generated.h"

class UAbilitySystemComponent;
class ACharacter;
class UCharacterMovementComponent;

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
	void RefreshWeaponState();

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

	/**
	 * 当前动画实例所属的 Character。
	 *
	 * 作用：
	 * - 统一在 C++ 层缓存角色引用，避免每个 ABP/LinkedAnimLayer 都在事件图里重复 TryGetPawnOwner + Cast。
	 * - ABP_ItemAnimLayersBase 继承此类后，Sword/Gloves 等子动画层也能直接读取同一份角色状态数据。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	TObjectPtr<ACharacter> NativeCharacter = nullptr;

	/**
	 * 当前角色的 CharacterMovementComponent。
	 *
	 * 动画蓝图可用它读取更细的移动状态；常用数据已经在下面的 NativeVelocity、
	 * NativeGroundSpeed、NativeDirection、bNativeShouldMove、bNativeIsFalling 中计算好。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	TObjectPtr<UCharacterMovementComponent> NativeMovementComponent = nullptr;

	/**
	 * 当前角色是否装备了任意武器。
	 *
	 * 数据来源：ASC 是否拥有 Status.Weapon.Equipped 标签。
	 * 用法：ABP_Character_Base 里直接把它接到 Blend Poses by Bool，
	 * false 走空手移动状态机，true 走武器动画分层。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	bool bHasWeapon = false;

	/** 缓存 OwningActor 上的 ASC，避免 ABP 每帧自己 Cast/查找。 */
	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> NativeAbilitySystemComponent = nullptr;

	/** 当前移动组件速度，直接来自 CharacterMovementComponent::Velocity。 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	FVector NativeVelocity = FVector::ZeroVector;

	/**
	 * XY 平面速度，供移动 BlendSpace 的 Speed 轴使用。
	 *
	 * 注意：只计算水平速度，不包含 Z 轴，跳跃/下落时不会因为竖直速度误触发跑步动画。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	float NativeGroundSpeed = 0.0f;

	/**
	 * 速度相对于角色朝向的方向角，单位为度。
	 *
	 * 约定：
	 * - 0   = 正前
	 * - 90  = 右
	 * - -90 = 左
	 * - 180/-180 = 后
	 *
	 * 当 MovementComponent 开启 bOrientRotationToMovement 时，会按原 ABP 逻辑限制到 -45~45，
	 * 避免非锁定移动时转身误播后退/侧移资源。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	float NativeDirection = 0.0f;

	/**
	 * 是否应该进入移动状态。
	 *
	 * 同时满足：
	 * - NativeGroundSpeed 大于 NativeShouldMoveThreshold
	 * - 当前有非零输入加速度
	 *
	 * 这和 UE 模板/原 ABP 的 ShouldMove 逻辑一致，用来过滤非常小的抖动速度。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	bool bNativeShouldMove = false;

	/** 是否处于下落/空中状态，直接来自 CharacterMovementComponent::IsFalling。 */
	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	bool bNativeIsFalling = false;

	/** 移动判定阈值，速度小于该值时不会认为角色正在移动。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character State Data")
	float NativeShouldMoveThreshold = 3.0f;
};
