// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "GameplayTagStack.generated.h"

struct FGameplayTagStackContainer;
struct FNetDeltaSerializeInfo;

/**
 * FGameplayTagStack — 单个 GameplayTag 的堆叠计数（参考 Lyra FGameplayTagStack）
 *
 * 每个条目 = 一个 Tag + 计数值，例如：
 *   {Tag="Status.Burning", StackCount=3} 表示 "燃烧" 层数 = 3
 *
 * 继承 FFastArraySerializerItem，支持通过 FastArray 网络复制
 */
USTRUCT(BlueprintType)
struct FGameplayTagStack : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FGameplayTagStack()
	{}

	/** 构造：指定 Tag 和初始计数值 */
	FGameplayTagStack(FGameplayTag InTag, int32 InStackCount)
		: Tag(InTag)
		, StackCount(InStackCount)
	{
	}

	/** 调试字符串，格式 "TagName×Count" */
	FString GetDebugString() const;

private:
	friend FGameplayTagStackContainer;

	/** 标签 */
	UPROPERTY()
	FGameplayTag Tag;

	/** 当前堆叠数量 */
	UPROPERTY()
	int32 StackCount = 0;
};

/**
 * FGameplayTagStackContainer — GameplayTag 堆叠容器（参考 Lyra FGameplayTagStackContainer）
 *
 * 管理一组 GameplayTag 的堆叠计数，支持网络复制。
 *
 * 内部维护双重数据结构：
 *   1. TArray<FGameplayTagStack> Stacks — 用于网络复制（FastArray）
 *   2. TMap<FGameplayTag, int32> TagToCountMap — 用于 O(1) 快速查询
 *
 * 典型用途：
 *   - 阵营标签（如 "Team.HasCapturePoint", "Team.IsWinning"）
 *   - 角色状态标签（如 "Status.Poisoned", "Status.Shielded"）
 *   - 物品标签（如 "Inventory.HasAmmo", "Inventory.HasKey"）
 *
 * 使用示例：
 *   TeamTags.AddStack(FGameplayTag("Status.Poisoned"), 3);   // 添加3层中毒
 *   int32 Count = TeamTags.GetStackCount(Tag);               // 查询层数
 *   TeamTags.RemoveStack(FGameplayTag("Status.Poisoned"), 1); // 移除1层
 *   bool bHas = TeamTags.ContainsTag(Tag);                    // 是否有该标签
 */
USTRUCT(BlueprintType)
struct FGameplayTagStackContainer : public FFastArraySerializer
{
	GENERATED_BODY()

	FGameplayTagStackContainer()
	{
	}

public:
	/**
	 * 添加指定数量的堆叠
	 * @param Tag       要添加的标签
	 * @param StackCount 要添加的数量（必须 >= 1，否则忽略）
	 *
	 * 如果标签已存在，累加计数；否则新增一条记录。
	 */
	void AddStack(FGameplayTag Tag, int32 StackCount);

	/**
	 * 移除指定数量的堆叠
	 * @param Tag       要移除的标签
	 * @param StackCount 要移除的数量（必须 >= 1，否则忽略）
	 *
	 * 如果剩余数量 <= 0，完全删除该标签条目；
	 * 如果剩余数量 > 0，仅减少计数。
	 */
	void RemoveStack(FGameplayTag Tag, int32 StackCount);

	/** 查询指定标签的堆叠数量（不存在返回 0），O(1) 查询 */
	int32 GetStackCount(FGameplayTag Tag) const
	{
		return TagToCountMap.FindRef(Tag);
	}

	/** 判断是否存在至少 1 层指定标签 */
	bool ContainsTag(FGameplayTag Tag) const
	{
		return TagToCountMap.Contains(Tag);
	}

	// ===== FFastArraySerializer 生命周期回调 =====
	// 引擎在网络复制删除/新增/修改元素时自动调用，用于同步 TagToCountMap

	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	/** FastArray 网络序列化入口 */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FGameplayTagStack, FGameplayTagStackContainer>(
			Stacks, DeltaParms, *this);
	}

private:
	// ===== 网络复制用的数组 =====
	UPROPERTY()
	TArray<FGameplayTagStack> Stacks;

	// ===== 本地 O(1) 查询缓存 =====
	// 始终保持与 Stacks 同步（增/删/改时同步更新）
	TMap<FGameplayTag, int32> TagToCountMap;
};

/** 启用 FastArray NetDeltaSerializer */
template<>
struct TStructOpsTypeTraits<FGameplayTagStackContainer> : public TStructOpsTypeTraitsBase2<FGameplayTagStackContainer>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};
