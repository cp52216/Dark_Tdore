// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamSubsystem.h"
#include "Dark_TdoreTeamAgentInterface.h"
#include "Dark_TdoreTeamInfoBase.h"
#include "Dark_TdoreTeamPublicInfo.h"
#include "AbilitySystemGlobals.h"
#include "Dark_TdoreLogChannels.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Player/Dark_TdorePlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamSubsystem)

// ============ FDark_TdoreTeamTrackingInfo ============

// SetTeamInfo — 注册阵营信息 Actor 到此追踪条目
// 提取显示资产，若资产变更则广播委托
void FDark_TdoreTeamTrackingInfo::SetTeamInfo(ADark_TdoreTeamInfoBase* Info)
{
	if (ADark_TdoreTeamPublicInfo* NewPublicInfo = Cast<ADark_TdoreTeamPublicInfo>(Info))
	{
		PublicInfo = NewPublicInfo;
		UDark_TdoreTeamDisplayAsset* OldDisplayAsset = DisplayAsset;
		DisplayAsset = NewPublicInfo->GetTeamDisplayAsset();
		if (OldDisplayAsset != DisplayAsset)
		{
			OnTeamDisplayAssetChanged.Broadcast(DisplayAsset);
		}
	}
	else
	{
		PrivateInfo = Cast<ADark_TdoreTeamPublicInfo>(Info);
	}
}

// RemoveTeamInfo — 注销阵营信息 Actor
void FDark_TdoreTeamTrackingInfo::RemoveTeamInfo(ADark_TdoreTeamInfoBase* Info)
{
	if (PublicInfo == Info)      { PublicInfo = nullptr; }
	else if (PrivateInfo == Info) { PrivateInfo = nullptr; }
}

// ============ UDark_TdoreTeamSubsystem ============

void UDark_TdoreTeamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDark_TdoreTeamSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

// -------------------------------------------------------------------
// RegisterTeamInfo — 注册阵营信息
// TeamInfo 在 BeginPlay 时调用此方法，将自身注册到子系统
// 核心数据流：TeamInfo Actor → TeamMap[TeamId] → TrackingInfo
bool UDark_TdoreTeamSubsystem::RegisterTeamInfo(ADark_TdoreTeamInfoBase* TeamInfo)
{
	if (!ensure(TeamInfo)) return false;

	const int32 TeamId = TeamInfo->GetTeamId();
	if (ensure(TeamId != INDEX_NONE))
	{
		FDark_TdoreTeamTrackingInfo& Entry = TeamMap.FindOrAdd(TeamId);
		Entry.SetTeamInfo(TeamInfo);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------
// UnregisterTeamInfo — 注销阵营信息
// TeamInfo 在 EndPlay 时调用，从 TeamMap 中移除
bool UDark_TdoreTeamSubsystem::UnregisterTeamInfo(ADark_TdoreTeamInfoBase* TeamInfo)
{
	if (!ensure(TeamInfo)) return false;

	const int32 TeamId = TeamInfo->GetTeamId();
	if (ensure(TeamId != INDEX_NONE))
	{
		if (FDark_TdoreTeamTrackingInfo* Entry = TeamMap.Find(TeamId))
		{
			Entry->RemoveTeamInfo(TeamInfo);
			return true;
		}
	}
	return false;
}

// -------------------------------------------------------------------
// ChangeTeamForActor — 更改 Actor 所属阵营
// 支持 PlayerState 和任意 TeamAgentInterface 实现者
bool UDark_TdoreTeamSubsystem::ChangeTeamForActor(AActor* ActorToChange, int32 NewTeamIndex)
{
	const FGenericTeamId NewTeamID = IntegerToGenericTeamId(NewTeamIndex);
	if (ADark_TdorePlayerState* PS = const_cast<ADark_TdorePlayerState*>(Cast<ADark_TdorePlayerState>(ActorToChange)))
	{
		PS->SetGenericTeamId(NewTeamID);
		return true;
	}
	else if (IDark_TdoreTeamAgentInterface* TeamActor = Cast<IDark_TdoreTeamAgentInterface>(ActorToChange))
	{
		TeamActor->SetGenericTeamId(NewTeamID);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------
// FindTeamFromObject — 查找对象所属阵营 ID
// 查找优先级（从高到低）：
//   1. 自身实现 TeamAgentInterface → 直接查询
//   2. Actor 的 Instigator 实现了 TeamAgentInterface → 查询 Instigator
//   3. 自身是 TeamInfo Actor → 直接读 TeamId
//   4. Pawn/Controller → 通过 PlayerState 查询
int32 UDark_TdoreTeamSubsystem::FindTeamFromObject(const UObject* TestObject) const
{
	// 优先级 1：自身直接是 TeamAgent
	if (const IDark_TdoreTeamAgentInterface* TeamAgent = Cast<IDark_TdoreTeamAgentInterface>(TestObject))
	{
		return GenericTeamIdToInteger(TeamAgent->GetGenericTeamId());
	}

	// 优先级 2-4：Actor 深层探测
	if (const AActor* TestActor = Cast<const AActor>(TestObject))
	{
		// 2：Instigator 是 TeamAgent
		if (const IDark_TdoreTeamAgentInterface* InstigatorAgent = Cast<IDark_TdoreTeamAgentInterface>(TestActor->GetInstigator()))
		{
			return GenericTeamIdToInteger(InstigatorAgent->GetGenericTeamId());
		}

		// 3：自身是 TeamInfo Actor
		if (const ADark_TdoreTeamInfoBase* TeamInfo = Cast<ADark_TdoreTeamInfoBase>(TestActor))
		{
			return TeamInfo->GetTeamId();
		}

		// 4：通过 PlayerState 查询
		if (const APawn* Pawn = Cast<const APawn>(TestActor))
		{
			if (const ADark_TdorePlayerState* PS = Pawn->GetPlayerState<ADark_TdorePlayerState>())
			{
				if (const IDark_TdoreTeamAgentInterface* PSAgent = Cast<IDark_TdoreTeamAgentInterface>(PS))
				{
					return GenericTeamIdToInteger(PSAgent->GetGenericTeamId());
				}
			}
		}
	}

	return INDEX_NONE;  // 未找到任何阵营归属
}

// -------------------------------------------------------------------
// CompareTeams — 比较两个对象的阵营关系（带 ID 输出）
EDark_TdoreTeamComparison UDark_TdoreTeamSubsystem::CompareTeams(
	const UObject* A, const UObject* B, int32& TeamIdA, int32& TeamIdB) const
{
	TeamIdA = FindTeamFromObject(A);
	TeamIdB = FindTeamFromObject(B);

	// 至少一方无阵营 → 参数无效
	if (TeamIdA == INDEX_NONE || TeamIdB == INDEX_NONE)
		return EDark_TdoreTeamComparison::InvalidArgument;

	// 同 ID → 同阵营，否则 → 不同阵营
	return (TeamIdA == TeamIdB)
		? EDark_TdoreTeamComparison::OnSameTeam
		: EDark_TdoreTeamComparison::DifferentTeams;
}

// 精简版
EDark_TdoreTeamComparison UDark_TdoreTeamSubsystem::CompareTeams(const UObject* A, const UObject* B) const
{
	int32 TeamIdA, TeamIdB;
	return CompareTeams(A, B, TeamIdA, TeamIdB);
}

// -------------------------------------------------------------------
// CanCauseDamage — 友伤检测（DamageExecution 中调用）
//
// 核心逻辑：
//   1. bAllowDamageToSelf=true 且 Instigator==Target → 允许（自伤）
//   2. 双方不同阵营                          → 允许（敌人伤害）
//   3. 目标无阵营但有 ASC                    → 允许（训练假人/环境伤害）
//   4. 同阵营                                → 禁止（友伤阻止）
bool UDark_TdoreTeamSubsystem::CanCauseDamage(
	const UObject* Instigator, const UObject* Target, bool bAllowDamageToSelf) const
{
	// ===== 先对比阵营（即使自伤也查，方便日志诊断）=====
	int32 InstigatorTeamId, TargetTeamId;
	const EDark_TdoreTeamComparison Relationship = CompareTeams(
		Instigator, Target, InstigatorTeamId, TargetTeamId);

	UE_LOG(LogDark_TdoreGAS, Display, TEXT("[TeamSubsystem] CanCauseDamage: Instigator=%s(Team=%d) Target=%s(Team=%d) → %s | 自伤=%d"),
		*GetNameSafe(Instigator), InstigatorTeamId,
		*GetNameSafe(Target), TargetTeamId,
		Relationship == EDark_TdoreTeamComparison::OnSameTeam ? TEXT("同阵营-禁止") :
		Relationship == EDark_TdoreTeamComparison::DifferentTeams ? TEXT("不同阵营-允许") :
		TEXT("无阵营-允许"),
		bAllowDamageToSelf && (Instigator == Target) ? 1 : 0);

	// 规则 1：自伤检查
	if (bAllowDamageToSelf)
	{
		if (Instigator == Target) return true;
	}

	// 规则 2：不同阵营 → 允许
	if (Relationship == EDark_TdoreTeamComparison::DifferentTeams) return true;

	// 规则 3：目标无阵营但有 ASC（训练假人等非玩家目标）
	if (Relationship == EDark_TdoreTeamComparison::InvalidArgument && InstigatorTeamId != INDEX_NONE)
	{
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(
			Cast<const AActor>(Target)) != nullptr;
	}

	// 规则 4：同阵营 → 禁止
	return false;
}

// -------------------------------------------------------------------
// 显示资产相关

UDark_TdoreTeamDisplayAsset* UDark_TdoreTeamSubsystem::GetTeamDisplayAsset(int32 TeamId, int32 ViewerTeamId)
{
	if (FDark_TdoreTeamTrackingInfo* Entry = TeamMap.Find(TeamId))
	{
		return Entry->DisplayAsset;
	}
	return nullptr;
}

TArray<int32> UDark_TdoreTeamSubsystem::GetTeamIDs() const
{
	TArray<int32> Result;
	TeamMap.GenerateKeyArray(Result);
	Result.Sort();
	return Result;
}
