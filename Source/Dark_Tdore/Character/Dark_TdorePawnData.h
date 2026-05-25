// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#include "Dark_TdorePawnData.generated.h"

class UDark_TdoreAbilitySet;
class UDark_TdoreCameraMode;
class UDark_TdoreInputConfig;

/**
 * UDark_TdorePawnData — Pawn 配置 DataAsset（参考 Lyra ULyraPawnData）
 *
 * 捆绑 Pawn 所需的所有配置到一个 DataAsset：
 *  - AbilitySet         → 技能/GE/属性
 *  - InputConfig        → 输入映射
 *  - DefaultCameraMode  → 默认摄像机模式（第三人称）
 *
 * 好处：
 *  - 一个 DA 配置一个角色，不必在蓝图 CDO 上分散配多个属性
 *  - 切换角色配置只需换 DA，不用改蓝图
 *  - 为将来 GameFeature 热切换做准备
 */
UCLASS(MinimalAPI, BlueprintType, Const, Meta = (DisplayName = "Dark Tdore Pawn Data"))
class UDark_TdorePawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UDark_TdorePawnData(const FObjectInitializer& ObjectInitializer);

	/** 技能集合（GA/GE/AttributeSet） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TObjectPtr<UDark_TdoreAbilitySet>> AbilitySets;

	/** 默认摄像机模式（第三人称等） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	TSubclassOf<UDark_TdoreCameraMode> DefaultCameraMode;

	/** 输入配置 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UDark_TdoreInputConfig> InputConfig;
};
