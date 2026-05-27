// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Dark_TdoreTeamSubsystem.generated.h"

class ADark_TdoreTeamInfoBase;
class ADark_TdoreTeamPublicInfo;
class UDark_TdoreTeamDisplayAsset;
struct FGameplayTag;

// ============ 委派声明 ============

/** 阵营显示资产变更委托 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnDarkTdoreTeamDisplayAssetChangedDelegate,
	const UDark_TdoreTeamDisplayAsset*, DisplayAsset);

// ============ 阵营追踪信息 ============

/**
 * FDark_TdoreTeamTrackingInfo — 单一阵营的追踪信息
 *
 * 每个阵营（按 TeamId 索引）对应一个此结构体，存储在 TeamSubsystem 的 TMap 中。
 * 包含：
 *   - PublicInfo:  阵营公开信息 Actor（包含显示资产）
 *   - PrivateInfo: 阵营私有信息 Actor（仅服务器可见）
 *   - DisplayAsset: 显示资产缓存（从 PublicInfo 中提取）
 */
USTRUCT()
struct FDark_TdoreTeamTrackingInfo
{
	GENERATED_BODY()

	/** 阵营公开信息 Actor（所有客户端可见，含显示资产） */
	UPROPERTY()
	TObjectPtr<ADark_TdoreTeamPublicInfo> PublicInfo = nullptr;

	/** 阵营私有信息 Actor（仅服务器可见） */
	UPROPERTY()
	TObjectPtr<ADark_TdoreTeamPublicInfo> PrivateInfo = nullptr;

	/** 显示资产缓存（从 PublicInfo 提取，避免每次查询都走 Actor） */
	UPROPERTY()
	TObjectPtr<UDark_TdoreTeamDisplayAsset> DisplayAsset = nullptr;

	/** 显示资产变更委托 */
	UPROPERTY()
	FOnDarkTdoreTeamDisplayAssetChangedDelegate OnTeamDisplayAssetChanged;

	/** 注册阵营信息 Actor 到此追踪条目 */
	void SetTeamInfo(ADark_TdoreTeamInfoBase* Info);

	/** 注销阵营信息 Actor */
	void RemoveTeamInfo(ADark_TdoreTeamInfoBase* Info);
};

// ============ 阵营对比结果枚举 ============

/** 阵营对比结果 */
UENUM(BlueprintType)
enum class EDark_TdoreTeamComparison : uint8
{
	OnSameTeam,       // 双方属于同一阵营
	DifferentTeams,   // 双方属于不同阵营
	InvalidArgument   // 至少一方无阵营或参数无效
};

// ============ 阵营子系统 ============

/**
 * UDark_TdoreTeamSubsystem — 阵营子系统（参考 Lyra ULyraTeamSubsystem）
 *
 * 继承自 UWorldSubsystem，每个 World 自动创建一个实例。
 *
 * 核心职责：
 *   1. 阵营注册/注销 — TeamInfo Actor BeginPlay/EndPlay 时自动注册
 *   2. 阵营查询     — 根据任意对象（Pawn/PlayerState/Controller）查找所属阵营
 *   3. 阵营对比     — CompareTeams(A, B) → OnSameTeam / DifferentTeams
 *   4. 友伤判断     — CanCauseDamage(Instigator, Target) → 同阵营返回 false
 *   5. 显示资产管理 — 阵营颜色/纹理的获取与变更通知
 *
 * 使用流程：
 *   创建 DA_TeamRed → TeamCreationComponent 创建阵营 Actor → 子系统自动注册
 *   → DamageExecution 调用 CanCauseDamage 检查 → 同阵营伤害归零
 */
UCLASS(MinimalAPI)
class UDark_TdoreTeamSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ===== USubsystem 生命周期 =====
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ===== 阵营注册/注销 =====

	/**
	 * 注册阵营信息 Actor（TeamInfo BeginPlay 时自动调用）
	 * @param TeamInfo 阵营信息 Actor（PublicInfo 或 PrivateInfo）
	 * @return 是否注册成功
	 */
	bool RegisterTeamInfo(ADark_TdoreTeamInfoBase* TeamInfo);

	/**
	 * 注销阵营信息 Actor（TeamInfo EndPlay 时自动调用）
	 * @param TeamInfo 阵营信息 Actor
	 * @return 是否注销成功
	 */
	bool UnregisterTeamInfo(ADark_TdoreTeamInfoBase* TeamInfo);

	/**
	 * 更改 Actor 所属阵营（仅服务器）
	 * @param ActorToChange 要变更阵营的 Actor
	 * @param NewTeamId     新阵营 ID
	 * @return 是否变更成功
	 */
	bool ChangeTeamForActor(AActor* ActorToChange, int32 NewTeamId);

	// ===== 阵营查询 =====

	/**
	 * 查找对象所属阵营 ID
	 * @param TestObject 任意对象（Pawn / PlayerState / Controller / TeamInfo）
	 * @return 阵营 ID，无阵营返回 INDEX_NONE
	 */
	int32 FindTeamFromObject(const UObject* TestObject) const;

	/**
	 * 比较两个对象的阵营关系（蓝图可用，ExpandEnumAsExecs 输出分支）
	 * @param A/B     要比较的两个对象
	 * @param TeamIdA [out] A 的阵营 ID
	 * @param TeamIdB [out] B 的阵营 ID
	 * @return 阵营对比结果（OnSameTeam / DifferentTeams / InvalidArgument）
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Teams, meta = (ExpandEnumAsExecs = ReturnValue))
	EDark_TdoreTeamComparison CompareTeams(const UObject* A, const UObject* B, int32& TeamIdA, int32& TeamIdB) const;

	/** 精简版：只返回阵营关系（不输出 TeamId） */
	EDark_TdoreTeamComparison CompareTeams(const UObject* A, const UObject* B) const;

	// ===== 友伤判断 =====

	/**
	 * 判断攻击者是否可以对目标造成伤害（友伤检测核心）
	 *
	 * 规则：
	 *   1. bAllowDamageToSelf=true 且 Instigator==Target → 允许（自伤）
	 *   2. 双方不同阵营                                     → 允许
	 *   3. 目标无阵营但有 ASC（训练假人等）                 → 允许
	 *   4. 同阵营                                           → 禁止
	 *
	 * @param Instigator         攻击者
	 * @param Target             目标
	 * @param bAllowDamageToSelf 是否允许自伤
	 * @return true=可以造成伤害，false=友伤阻止
	 */
	bool CanCauseDamage(const UObject* Instigator, const UObject* Target, bool bAllowDamageToSelf = true) const;

	// ===== 显示资产 =====

	/** 获取阵营显示资产 */
	UDark_TdoreTeamDisplayAsset* GetTeamDisplayAsset(int32 TeamId, int32 ViewerTeamId);

	/** 阵营是否存在 */
	bool DoesTeamExist(int32 TeamId) const { return TeamMap.Contains(TeamId); }

	/** 获取所有已注册的阵营 ID */
	TArray<int32> GetTeamIDs() const;

private:
	/** 阵营 ID → 追踪信息 映射表（核心数据结构） */
	UPROPERTY()
	TMap<int32, FDark_TdoreTeamTrackingInfo> TeamMap;
};
