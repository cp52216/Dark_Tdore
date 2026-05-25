// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGameplayEffectContext.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

// ============ Iris 网络序列化 — UE 5.x 必须 ============
// FGameplayEffectContext 在新版 UE 中需要通过 Iris 网络系统进行序列化。
// 下面的 include + UE_NET_IMPLEMENT_FORWARDING 将我们的自定义 Context
// 的序列化转发给引擎内置的 FGameplayEffectContextNetSerializer。
// 
// 重要提示：如果未来在 NetSerialize() 中新增了自定义字段的网络同步，
//           必须实现自定义 NetSerializer，不能再使用此转发方式。
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Serialization/GameplayEffectContextNetSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGameplayEffectContext)

// ============ ExtractEffectContext — 类型安全的向下转型 ============
// 参考 Lyra FLyraGameplayEffectContext::ExtractEffectContext
//
// 从 FGameplayEffectContextHandle 中提取我们的扩展类型。
// 先通过 GetScriptStruct() 检查运行时类型，只有确认是 FDark_TdoreGameplayEffectContext
// 或其子类才执行 static_cast，否则返回 nullptr。
//
// 使用场景：ExecutionCalculation 中调用此方法获取 HitResult、CartridgeID 等扩展数据。
FDark_TdoreGameplayEffectContext* FDark_TdoreGameplayEffectContext::ExtractEffectContext(FGameplayEffectContextHandle Handle)
{
	FGameplayEffectContext* BaseEffectContext = Handle.Get();
	if ((BaseEffectContext != nullptr) && BaseEffectContext->GetScriptStruct()->IsChildOf(FDark_TdoreGameplayEffectContext::StaticStruct()))
	{
		return static_cast<FDark_TdoreGameplayEffectContext*>(BaseEffectContext);
	}

	// 类型不匹配：可能是标准 FGameplayEffectContext，也可能是其他项目的扩展类型。
	// 调用方必须处理 nullptr 的情况。
	return nullptr;
}

// ============ NetSerialize — 网络序列化 ============
// 参考 Lyra FLyraGameplayEffectContext::NetSerialize
//
// 资产激活阶段的数据通过父类序列化（Instigator、EffectCauser、SourceObject 等）。
// CartridgeID 目前仅在服务端使用（标记同一轮攻击的多个弹丸），
// 不需要复制到客户端，因此不参与序列化。
//
// 如果未来需要在客户端使用 CartridgeID（如：客户端预测伤害特效），
// 需要在此方法中添加 CartridgeID 的序列化代码，并实现自定义 NetSerializer。
bool FDark_TdoreGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// 父类序列化：Instigator、EffectCauser、SourceObject、HitResult 等
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	// CartridgeID 暂不序列化 — 仅服务端使用

	return true;
}

// ============ Iris 网络序列化转发 ============
// 参考 Lyra namespace UE::Net
//
// Iris 是 UE 5.x 的新网络序列化框架，替代了传统的 FNetGUID/UPackageMap。
// 此宏将我们的 FDark_TdoreGameplayEffectContext 注册给 Iris，
// 并转发到 FGameplayEffectContextNetSerializer（引擎内置）。
//
// WARNING: 如果修改了 FDark_TdoreGameplayEffectContext::NetSerialize() 的序列化内容，
//          这里的转发将不再正确。届时需要实现自定义 NetSerializer。
namespace UE::Net
{
	UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES(Dark_TdoreGameplayEffectContext, FGameplayEffectContextNetSerializer);
}

// ============ GetPhysicalMaterial — 从 HitResult 获取物理材质 ============
// 参考 Lyra FLyraGameplayEffectContext::GetPhysicalMaterial
//
// EffectContext 中可能携带 HitResult（命中扫描结果）。
// 通过 HitResult 的 PhysMaterial 字段可以获取被击中表面的物理材质。
//
// 用途（后续接入 AbilitySourceInterface 后启用）：
//   伤害计算中的 PhysicalMaterialAttenuation：
//     击中金属甲 → 伤害衰减不同
//     击中肉体   → 伤害衰减不同
//
// 返回 nullptr 的条件：
//   1. EffectContext 中没有 HitResult（如：纯数据 GE，非投射/扫描技能）
//   2. HitResult 中没有设置 PhysMaterial
const UPhysicalMaterial* FDark_TdoreGameplayEffectContext::GetPhysicalMaterial() const
{
	if (const FHitResult* HitResultPtr = GetHitResult())
	{
		return HitResultPtr->PhysMaterial.Get();
	}
	return nullptr;
}
