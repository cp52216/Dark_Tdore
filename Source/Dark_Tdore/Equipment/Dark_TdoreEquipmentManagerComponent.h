// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Dark_TdoreAbilitySet.h"
#include "Components/PawnComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Dark_TdoreEquipmentManagerComponent.generated.h"

class UActorComponent;
class UDark_TdoreAbilitySystemComponent;
class UDark_TdoreEquipmentDefinition;
class UDark_TdoreEquipmentInstance;
class FLifetimeProperty;
struct FReplicationFlags;

USTRUCT(BlueprintType)
struct FDark_TdoreAppliedEquipmentEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 调试输出：显示装备实例和装备定义名称。 */
	FString GetDebugString() const;

private:
	friend struct FDark_TdoreEquipmentList;
	friend class UDark_TdoreEquipmentManagerComponent;

	/** 当前条目对应的装备定义类。客户端通过复制该条目知道装备类型。 */
	UPROPERTY()
	TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition;

	/** 当前装备的运行时实例，负责 OnEquipped/OnUnequipped、挂件 Actor 等行为。 */
	UPROPERTY()
	TObjectPtr<UDark_TdoreEquipmentInstance> Instance = nullptr;

	/** 服务器上记录由该装备授予的 Ability/GE 句柄，卸下装备时用于精确回收。 */
	UPROPERTY(NotReplicated)
	FDark_TdoreAbilitySet_GrantedHandles GrantedHandles;
};

/**
 * 可复制装备列表。
 *
 * 使用 FastArraySerializer 的原因：
 * - 装备列表是动态数组，可能增删。
 * - FastArray 能高效同步增删变化，并在客户端触发 PostReplicatedAdd/PreReplicatedRemove。
 * - 客户端收到新增装备时调用 OnEquipped，收到移除时调用 OnUnequipped。
 */
USTRUCT(BlueprintType)
struct FDark_TdoreEquipmentList : public FFastArraySerializer
{
	GENERATED_BODY()

	FDark_TdoreEquipmentList()
		: OwnerComponent(nullptr)
	{
	}

	FDark_TdoreEquipmentList(UActorComponent* InOwnerComponent)
		: OwnerComponent(InOwnerComponent)
	{
	}

	/** 客户端移除装备条目前调用，用于播放卸下回调、清理视觉状态。 */
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	/** 客户端新增装备条目后调用，用于播放装备回调。 */
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	/** 当前没有特殊变更逻辑，保留接口便于后续扩展耐久、弹药等同步字段。 */
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	/** FastArray 网络序列化入口。 */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FDark_TdoreAppliedEquipmentEntry, FDark_TdoreEquipmentList>(Entries, DeltaParms, *this);
	}

	/** 服务器添加装备条目：创建实例、授予 AbilitySet、生成挂接 Actor。 */
	UDark_TdoreEquipmentInstance* AddEntry(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition);
	/** 服务器移除装备条目：回收 AbilitySet、销毁挂接 Actor、标记数组脏。 */
	void RemoveEntry(UDark_TdoreEquipmentInstance* Instance);

private:
	/** 从 OwnerComponent 所属 Actor 上获取项目 ASC。 */
	UDark_TdoreAbilitySystemComponent* GetAbilitySystemComponent() const;

	friend class UDark_TdoreEquipmentManagerComponent;

	/** 复制的装备条目数组。 */
	UPROPERTY()
	TArray<FDark_TdoreAppliedEquipmentEntry> Entries;

	/** 拥有该装备列表的组件，不复制，只在本地用于获取 Owner/ASC。 */
	UPROPERTY(NotReplicated)
	TObjectPtr<UActorComponent> OwnerComponent;
};

template<>
struct TStructOpsTypeTraits<FDark_TdoreEquipmentList> : public TStructOpsTypeTraitsBase2<FDark_TdoreEquipmentList>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * 装备管理组件，参考 Lyra EquipmentManagerComponent 的项目化版本。
 *
 * 挂载位置：
 * - 当前已默认创建在 ADark_TdoreCharacter 上。
 *
 * 责任边界：
 * - 管理当前 Pawn 已装备的装备列表。
 * - 装备/卸下时创建或销毁 EquipmentInstance。
 * - 装备/卸下时授予或回收 AbilitySet。
 * - 复制装备实例给客户端。
 *
 * 不负责：
 * - 背包格子。
 * - 快捷栏选择。
 * - 拾取交互。
 *
 * 这些会在后续 Inventory/QuickBar 阶段接入。
 */
UCLASS(BlueprintType, Const, meta = (BlueprintSpawnableComponent))
class UDark_TdoreEquipmentManagerComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 服务器装备一件装备定义，返回创建出来的装备实例。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	UDark_TdoreEquipmentInstance* EquipItem(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition);

	/** 服务器卸下指定装备实例。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipItem(UDark_TdoreEquipmentInstance* ItemInstance);

	virtual bool ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void ReadyForReplication() override;

	/** 获取第一件指定实例类型的装备，例如查找当前装备的 SwordWeaponInstance。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Equipment")
	UDark_TdoreEquipmentInstance* GetFirstInstanceOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType);

	/** 获取所有指定实例类型的装备，适合双持武器或多个同类装备。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Equipment")
	TArray<UDark_TdoreEquipmentInstance*> GetEquipmentInstancesOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType) const;

	/** 按装备定义查找已经装备的实例。按 2/3/4 切武器时用它判断目标武器是否已经在手上。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Equipment")
	UDark_TdoreEquipmentInstance* GetFirstInstanceOfDefinition(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition) const;

	/** 卸下所有指定实例类型的装备。武器槽切换时用来保证当前只持有一把主武器。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipAllItemsOfType(TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType);

	template <typename T>
	T* GetFirstInstanceOfType()
	{
		return Cast<T>(GetFirstInstanceOfType(T::StaticClass()));
	}

private:
	/** 当前装备列表，使用 FastArray 复制。 */
	UPROPERTY(Replicated)
	FDark_TdoreEquipmentList EquipmentList;
};
