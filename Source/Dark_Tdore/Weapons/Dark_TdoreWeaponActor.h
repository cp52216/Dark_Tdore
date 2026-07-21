// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dark_TdoreWeaponActor.generated.h"

class APawn;
class UDark_TdoreEquipmentInstance;
class USceneComponent;
class UStaticMeshComponent;

/**
 * 武器可见 Actor 基类（参考 Lyra 武器 Actor 模式）
 *
 * 职责分离：
 *  - ADark_TdoreWeaponActor   → 拥有表现层组件（StaticMesh、碰撞体等）
 *  - UDark_TdoreWeaponInstance → 拥有运行时装备逻辑（动画层、装备/卸下回调、时间记录）
 *
 * 生命周期：
 *  EquipmentManager::EquipItem() → SpawnActor<ADark_TdoreWeaponActor>
 *                               → AttachToComponent(Character->GetMesh(), Socket)
 *                               → InitializeFromEquipmentInstance(WeaponInstance)
 *                               → K2_OnInitializedFromEquipmentInstance（蓝图扩展点）
 */
UCLASS(Blueprintable, BlueprintType)
class ADark_TdoreWeaponActor : public AActor
{
	GENERATED_BODY()

public:
	ADark_TdoreWeaponActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;

	/** 由装备实例在 Actor 生成并挂接到角色后立即调用，绑定装备实例并通知蓝图。 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitializeFromEquipmentInstance(UDark_TdoreEquipmentInstance* InEquipmentInstance);

	/** 获取拥有此武器 Actor 的装备实例。 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	UDark_TdoreEquipmentInstance* GetOwningEquipmentInstance() const { return OwningEquipmentInstance; }

	/** 获取装备此武器的 Pawn。 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	APawn* GetOwningPawn() const;

	/** 获取武器的 StaticMesh 组件，可用于近战碰撞检测或调试拾取预览。 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	UStaticMeshComponent* GetWeaponMeshComponent() const { return WeaponMeshComponent; }

	/** 切换武器碰撞：近战 Trace 时启用，装备表现状态时默认关闭。 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetWeaponCollisionEnabled(bool bEnabled);

	/** 切换武器可见性，用于装备/卸下过渡或背包隐藏。 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetWeaponVisible(bool bVisible);

protected:
	/** 蓝图扩展点：装备实例初始化完成后调用，可用于设置材质、粒子等表现。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon", meta = (DisplayName = "On Initialized From Equipment Instance"))
	void K2_OnInitializedFromEquipmentInstance(UDark_TdoreEquipmentInstance* InEquipmentInstance);

private:
	/** 武器根组件，作为挂接点和 StaticMesh 的父节点。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> WeaponRootComponent;

	/** 武器的可见网格体组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> WeaponMeshComponent;

	/** BeginPlay 时是否自动关闭碰撞（装备表现 Actor 通常不需要碰撞）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	bool bDisableCollisionOnBeginPlay = true;

	/** 缓存的装备实例引用，用于查询 Pawn、动画层等。 */
	UPROPERTY(Transient)
	TObjectPtr<UDark_TdoreEquipmentInstance> OwningEquipmentInstance;
};
