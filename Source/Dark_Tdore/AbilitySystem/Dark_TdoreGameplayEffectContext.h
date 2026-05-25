// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectTypes.h"

#include "Dark_TdoreGameplayEffectContext.generated.h"

class AActor;
class FArchive;
class UObject;
class UPhysicalMaterial;

/**
 * FDark_TdoreGameplayEffectContext — 扩展 GameplayEffectContext（参考 Lyra FLyraGameplayEffectContext）
 *
 * 在标准 FGameplayEffectContext 基础上增加：
 *   - CartridgeID：同一轮攻击中多发子弹的标识
 *   - AbilitySourceObject：（未来）用于 DistanceAttenuation / PhysicalMaterialAttenuation
 *
 * 使用方式：
 *   ASC 覆写 MakeEffectContext() → new FDark_TdoreGameplayEffectContext()
 *   ExecutionCalculation 中 ExtractEffectContext() → 获取扩展数据
 */
USTRUCT()
struct FDark_TdoreGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	FDark_TdoreGameplayEffectContext()
		: FGameplayEffectContext()
	{
	}

	FDark_TdoreGameplayEffectContext(AActor* InInstigator, AActor* InEffectCauser)
		: FGameplayEffectContext(InInstigator, InEffectCauser)
	{
	}

	/** 从 Handle 中提取本类型（类型安全检查后向下转型） */
	static FDark_TdoreGameplayEffectContext* ExtractEffectContext(struct FGameplayEffectContextHandle Handle);

	/** 复制工厂（浅复制 + 深复制 HitResult） */
	virtual FGameplayEffectContext* Duplicate() const override
	{
		FDark_TdoreGameplayEffectContext* NewContext = new FDark_TdoreGameplayEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}

	/** 返回此结构体的 UScriptStruct（引擎用于类型识别和序列化） */
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FDark_TdoreGameplayEffectContext::StaticStruct();
	}

	/** 网络序列化（继承父类序列化，新增字段此版暂不序列化） */
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;

	/** 从 HitResult 获取物理材质 */
	const UPhysicalMaterial* GetPhysicalMaterial() const;

public:
	/** 弹药 ID：标记同一轮攻击（如霰弹枪多发弹丸） */
	UPROPERTY()
	int32 CartridgeID = -1;

protected:
	/** 能力来源对象（未来配合 AbilitySourceInterface 使用，当前仅占位） */
	UPROPERTY()
	TWeakObjectPtr<const UObject> AbilitySourceObject;
};

/** TStructOpsTypeTraits：启用 NetSerializer + Copy */
template<>
struct TStructOpsTypeTraits<FDark_TdoreGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FDark_TdoreGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
