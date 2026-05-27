// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTagStack.h"
#include "UObject/Stack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayTagStack)

// ============ FGameplayTagStack::GetDebugString ============
// 返回 "标签名×数量" 格式的调试字符串，如 "Status.Poisoned×3"

FString FGameplayTagStack::GetDebugString() const
{
	return FString::Printf(TEXT("%s×%d"), *Tag.ToString(), StackCount);
}

// ============ FGameplayTagStackContainer ============

// -------------------------------------------------------------------
// AddStack — 添加堆叠
// -------------------------------------------------------------------
// 算法：
//   1. 遍历 Stacks 数组查找已有标签
//   2. 找到 → 累加计数，同步更新 TagToCountMap，标记脏数据
//   3. 未找到 → 新增一条记录，同步更新 TagToCountMap，标记脏数据
void FGameplayTagStackContainer::AddStack(FGameplayTag Tag, int32 StackCount)
{
	if (!Tag.IsValid())
	{
		FFrame::KismetExecutionMessage(TEXT("AddStack 收到了无效 Tag"), ELogVerbosity::Warning);
		return;
	}

	if (StackCount > 0)
	{
		// Step 1: 查找是否已有该 Tag 的堆叠
		for (FGameplayTagStack& Stack : Stacks)
		{
			if (Stack.Tag == Tag)
			{
				// Step 2: 已有 → 累加
				const int32 NewCount = Stack.StackCount + StackCount;
				Stack.StackCount = NewCount;
				TagToCountMap[Tag] = NewCount;
				MarkItemDirty(Stack);
				return;
			}
		}

		// Step 3: 没有 → 新建
		FGameplayTagStack& NewStack = Stacks.Emplace_GetRef(Tag, StackCount);
		MarkItemDirty(NewStack);
		TagToCountMap.Add(Tag, StackCount);
	}
}

// -------------------------------------------------------------------
// RemoveStack — 移除堆叠
// -------------------------------------------------------------------
// 算法：
//   1. 遍历 Stacks 查找标签
//   2. 找到且剩余量 <= 0 → 完全删除该条目 + TagToCountMap，标记数组脏数据
//   3. 找到且剩余量 > 0  → 减少计数，标记元素脏数据
void FGameplayTagStackContainer::RemoveStack(FGameplayTag Tag, int32 StackCount)
{
	if (!Tag.IsValid())
	{
		FFrame::KismetExecutionMessage(TEXT("RemoveStack 收到了无效 Tag"), ELogVerbosity::Warning);
		return;
	}

	if (StackCount > 0)
	{
		for (auto It = Stacks.CreateIterator(); It; ++It)
		{
			FGameplayTagStack& Stack = *It;
			if (Stack.Tag == Tag)
			{
				if (Stack.StackCount <= StackCount)
				{
					// 全部移除
					It.RemoveCurrent();
					TagToCountMap.Remove(Tag);
					MarkArrayDirty();
				}
				else
				{
					// 部分移除
					const int32 NewCount = Stack.StackCount - StackCount;
					Stack.StackCount = NewCount;
					TagToCountMap[Tag] = NewCount;
					MarkItemDirty(Stack);
				}
				return;
			}
		}
	}
}

// ============ FastArray 网络复制回调 ============
// 引擎在网络复制阶段调用，用于同步 TagToCountMap 与 Stacks 数组

void FGameplayTagStackContainer::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	// 删除前：从 TagToCountMap 中同步移除对应的 Tag
	for (int32 Index : RemovedIndices)
	{
		const FGameplayTag Tag = Stacks[Index].Tag;
		TagToCountMap.Remove(Tag);
	}
}

void FGameplayTagStackContainer::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	// 新增后：将新条目同步到 TagToCountMap
	for (int32 Index : AddedIndices)
	{
		const FGameplayTagStack& Stack = Stacks[Index];
		TagToCountMap.Add(Stack.Tag, Stack.StackCount);
	}
}

void FGameplayTagStackContainer::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	// 修改后：更新 TagToCountMap 中的计数值
	for (int32 Index : ChangedIndices)
	{
		const FGameplayTagStack& Stack = Stacks[Index];
		TagToCountMap[Stack.Tag] = Stack.StackCount;
	}
}
