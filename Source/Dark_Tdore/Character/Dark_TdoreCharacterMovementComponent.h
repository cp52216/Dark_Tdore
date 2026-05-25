// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "Dark_TdoreCharacterMovementComponent.generated.h"

/**
 * FDark_TdoreCharacterGroundInfo — 角色地面信息（缓存）
 * 参考 Lyra: FLyraCharacterGroundInfo
 */
USTRUCT(BlueprintType)
struct FDark_TdoreCharacterGroundInfo
{
	GENERATED_BODY()

	FDark_TdoreCharacterGroundInfo()
		: LastUpdateFrame(0)
		, GroundDistance(0.0f)
	{}

	uint64 LastUpdateFrame = 0;

	UPROPERTY(BlueprintReadOnly)
	FHitResult GroundHitResult;

	UPROPERTY(BlueprintReadOnly)
	float GroundDistance = 0.0f;
};

/**
 * UDark_TdoreCharacterMovementComponent — 自定义角色移动组件
 * 参考 Lyra: ULyraCharacterMovementComponent
 *
 * 核心增强:
 *   - ASC 感知: GetMaxSpeed/GetDeltaRotation 检查 GameplayTag
 *   - 地面信息缓存: GetGroundInfo() 带 Raycast
 *   - 网络加速复制: SetReplicatedAcceleration
 */
UCLASS(Config = Game)
class UDark_TdoreCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

	//~UMovementComponent 接口
	virtual FRotator GetDeltaRotation(float DeltaTime) const override;
	virtual float GetMaxSpeed() const override;
	virtual void SimulateMovement(float DeltaTime) override;
	//~End of UMovementComponent 接口

	//~UCharacterMovementComponent 接口
	virtual bool CanAttemptJump() const override;
	//~End of UCharacterMovementComponent 接口

	/** 获取缓存的地面信息（包含命中结果和距离） */
	UFUNCTION(BlueprintCallable, Category = "DarkTdore|Movement")
	const FDark_TdoreCharacterGroundInfo& GetGroundInfo();

	/** 设置网络复制的加速度（用于客户端预测） */
	void SetReplicatedAcceleration(const FVector& InAcceleration);

protected:
	virtual void InitializeComponent() override;

	/** 缓存的地面信息（通过 GetGroundInfo() 惰性更新） */
	FDark_TdoreCharacterGroundInfo CachedGroundInfo;

	/** 是否有网络复制的加速度需要保留 */
	UPROPERTY(Transient)
	bool bHasReplicatedAcceleration = false;
};
