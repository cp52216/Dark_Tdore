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
 * 装备运行时实例。
 *
 * 和 EquipmentDefinition 的区别：
 * - Definition 是配置，类似“这是什么装备”。
 * - Instance 是运行时对象，类似“某个角色当前装备着的这一件装备”。
 *
 * Instance 负责：
 * - 保存装备的 Instigator。
 * - 保存装备时生成出来的 Actor。
 * - 提供 OnEquipped/OnUnequipped 给蓝图扩展。
 * - 作为可复制子对象同步到客户端。
 */
UCLASS(BlueprintType, Blueprintable)
class UDark_TdoreEquipmentInstance : public UObject
{
	GENERATED_BODY()

public:
	UDark_TdoreEquipmentInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 装备实例是 UObject 子对象，需要显式声明支持网络复制。 */
	virtual bool IsSupportedForNetworking() const override { return true; }
	/** 让 UObject 能通过所属 Pawn 找到 World，方便 SpawnActor、计时等逻辑。 */
	virtual UWorld* GetWorld() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 谁创建/触发了这件装备，通常是装备拥有者 Actor。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	UObject* GetInstigator() const { return Instigator; }

	void SetInstigator(UObject* InInstigator) { Instigator = InInstigator; }

	/** 获取拥有这件装备的 Pawn。装备实例的 Outer 当前就是装备管理组件所属的 Pawn。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	APawn* GetPawn() const;

	/** 获取指定类型的 Pawn，蓝图里可以直接得到更具体的 Pawn 类型。 */
	UFUNCTION(BlueprintPure, Category = "Equipment", meta = (DeterminesOutputType = "PawnType"))
	APawn* GetTypedPawn(TSubclassOf<APawn> PawnType) const;

	/** 获取装备时生成并挂到角色身上的 Actor，例如剑模型、盾牌模型等。 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	TArray<AActor*> GetSpawnedActors() const { return SpawnedActors; }

	/** 根据 EquipmentDefinition.ActorsToSpawn 生成并挂接 Actor。 */
	virtual void SpawnEquipmentActors(const TArray<FDark_TdoreEquipmentActorToSpawn>& ActorsToSpawn);
	/** 卸下装备时销毁所有由该装备生成的 Actor。 */
	virtual void DestroyEquipmentActors();

	/** 装备完成后调用，C++ 子类可覆写，蓝图可实现 K2_OnEquipped。 */
	virtual void OnEquipped();
	/** 卸下完成前/后调用，C++ 子类可覆写，蓝图可实现 K2_OnUnequipped。 */
	virtual void OnUnequipped();

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "OnEquipped"))
	void K2_OnEquipped();

	UFUNCTION(BlueprintImplementableEvent, Category = "Equipment", meta = (DisplayName = "OnUnequipped"))
	void K2_OnUnequipped();

private:
	UFUNCTION()
	void OnRep_Instigator();

private:
	/** 装备触发者，复制给客户端，后续可用于 UI、特效归属、伤害来源等。 */
	UPROPERTY(ReplicatedUsing = OnRep_Instigator)
	TObjectPtr<UObject> Instigator;

	/** 装备时生成的可见 Actor 列表，复制给客户端并在卸下时销毁。 */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
