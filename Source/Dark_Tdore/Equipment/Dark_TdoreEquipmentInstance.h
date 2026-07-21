// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Dark_TdoreEquipmentDefinition.h"
#include "Dark_TdoreEquipmentInstance.generated.h"

class AActor;
class APawn;
class FLifetimeProperty;

/**
 * 装备运行时实例 — 装备系统的核心 UObject
 *
 * 和 EquipmentDefinition 的分工：
 *   ┌────────────────────────────────────────────────────┐
 *   │  UDark_TdoreEquipmentDefinition (DataAsset)        │
 *   │  静态配置：这是什么装备？                             │
 *   │  - InstanceType：用哪个 C++ 类来实例化                │
 *   │  - ActorsToSpawn：要生成哪些可见 Actor                │
 *   │  - AbilitySetsToGrant：装备后授予哪些技能              │
 *   └────────────────────────────────────────────────────┘
 *                         ↓ NewObject + 配置驱动
 *   ┌────────────────────────────────────────────────────┐
 *   │  UDark_TdoreEquipmentInstance (UObject)             │
 *   │  运行时对象：某个角色当前装备着的这一件                │
 *   │  - Instigator：谁触发的装备                          │
 *   │  - SpawnedActors：生成的武器模型等                   │
 *   │  - OnEquipped/OnUnequipped：装备/卸下回调            │
 *   │  - 可网络复制（UObject 子对象复制）                   │
 *   └────────────────────────────────────────────────────┘
 *
 * 生命周期：
 *   EquipmentManager::EquipItem()
 *       → NewObject<UDark_TdoreEquipmentInstance>(Owner, InstanceType)  // 创建
 *       → SpawnEquipmentActors()                                       // 生成 Actor
 *       → OnEquipped()                                                 // 装备回调
 *       → [网络复制到客户端]
 *       → OnUnequipped() + DestroyEquipmentActors()                    // 卸下
 *       → GC 回收
 */
UCLASS(BlueprintType, Blueprintable)
class UDark_TdoreEquipmentInstance : public UObject
{
	GENERATED_BODY()

public:
	UDark_TdoreEquipmentInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ============ 网络复制支持 ============

	/** 装备实例是 UObject 子对象，需要显式声明支持网络复制。
	 *  装备管理器组件通过 FASTARRAY + ReplicateSubobjects 或 RegisteredSubObjectList 同步实例。 */
	virtual bool IsSupportedForNetworking() const override { return true; }

	/** UObject 本身没有 World（不像 Actor），需要沿 Outer 链找到所属 Pawn 再取 World。
	 *  这让装备实例里可以安全地调用 SpawnActor、SetTimer 等需要 WorldContext 的操作。 */
	virtual UWorld* GetWorld() const override;

	/** 声明需要复制的属性：Instigator（触发者）和 SpawnedActors（生成的 Actor 列表）。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ============ 拥有者查询 ============

	/** 谁创建/触发了这件装备。通常是装备拥有者 Actor 本身。
	 *  客户端收到复制后，可用于 UI 显示、特效归属、伤害来源判定等。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	UObject* GetInstigator() const { return Instigator; }

	/** 设置触发者。在 AddEntry 中被调用，指向 OwnerActor。 */
	void SetInstigator(UObject* InInstigator) { Instigator = InInstigator; }

	/** 获取拥有这件装备的 Pawn。
	 *  装备管理组件创建实例时用 Owner Pawn 作为 Outer，
	 *  这里通过 GetOuter() 找回拥有者（不需要额外存一个 TObjectPtr）。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	APawn* GetPawn() const;

	/** 获取指定类型的 Pawn。蓝图里可以直接得到更具体的 Pawn 子类类型。
	 *  例如 GetTypedPawn(BP_ThirdPersonCharacter) 直接返回 ADark_TdoreCharacter*。 */
	UFUNCTION(BlueprintPure, Category = "Equipment", meta = (DeterminesOutputType = "PawnType"))
	APawn* GetTypedPawn(TSubclassOf<APawn> PawnType) const;

	// ============ 装备 Actor 管理 ============

	/** 获取装备时生成的可见 Actor 列表，例如剑模型、盾牌模型。
	 *  注意：这只是由本装备实例生成的 Actor，不包括角色原有组件。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	TArray<AActor*> GetSpawnedActors() const { return SpawnedActors; }

	/** 根据 EquipmentDefinition.ActorsToSpawn 配置生成并挂接 Actor。
	 *  对 Character 优先挂到 Mesh（让骨骼插槽生效），非 Character 挂到 RootComponent。
	 *  如果 Actor 是 ADark_TdoreWeaponActor 类型，还会调用 InitializeFromEquipmentInstance 绑定装备实例。 */
	virtual void SpawnEquipmentActors(const TArray<FDark_TdoreEquipmentActorToSpawn>& ActorsToSpawn);

	/** 卸下装备时销毁所有由该装备生成的 Actor。
	 *  Actor::Destroy() 会触发 EndPlay 等清理逻辑，不直接 delete。 */
	virtual void DestroyEquipmentActors();

	// ============ 装备/卸下回调 ============

	/** 装备完成后被 EquipmentManager::EquipItem 调用。
	 *  C++ 子类（如 UDark_TdoreWeaponInstance）覆写此方法来设置 Tag、挂载动画层。
	 *  基类在这里调用蓝图事件 K2_OnEquipped。 */
	virtual void OnEquipped();

	/** 卸下时被 EquipmentManager::UnequipItem / PreReplicatedRemove 调用。
	 *  C++ 子类覆写此方法来做清理（移除 Tag、卸载动画层）。
	 *  基类在这里调用蓝图事件 K2_OnUnequipped。 */
	virtual void OnUnequipped();

protected:
	/** 蓝图扩展点：装备完成后调用。蓝图子类可在此添加特效、UI 等表现层逻辑。
	 *  注意：网络环境下服务器和客户端都会触发（FastArray PostReplicatedAdd + 服务器本地调用）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "OnEquipped"))
	void K2_OnEquipped();

	/** 蓝图扩展点：卸下时调用。蓝图子类可在此清理表现层（隐藏特效、关闭 UI 等）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "OnUnequipped"))
	void K2_OnUnequipped();

private:
	/** Instigator 复制到客户端后的回调。当前未使用，预留扩展：例如客户端在此同步 UI。 */
	UFUNCTION()
	void OnRep_Instigator();

private:
	/** 装备触发者。复制给客户端，后续可用于 UI、特效归属、伤害来源等。
	 *  使用 ReplicatedUsing 是为了在未来有变化时能做客户端回调。 */
	UPROPERTY(ReplicatedUsing = OnRep_Instigator)
	TObjectPtr<UObject> Instigator;

	/** 装备时生成的可见 Actor 列表。
	 *  复制给客户端，这样客户端的 SpawnedActors 数组和服务器一致。
	 *  卸下时 DestroyEquipmentActors 会清空并销毁这些 Actor。 */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
