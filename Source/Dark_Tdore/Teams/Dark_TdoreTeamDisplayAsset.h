// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Dark_TdoreTeamDisplayAsset.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class UNiagaraComponent;
class AActor;
class UTexture;

/**
 * UDark_TdoreTeamDisplayAsset — 阵营显示资产（参考 Lyra ULyraTeamDisplayAsset）
 *
 * 一个 DataAsset 存储阵营的所有视觉参数，支持：
 *   - ScalarParameters: 标量参数（如 "Opacity=0.5"）
 *   - ColorParameters:  颜色参数（如 "TeamColor=(R=1,G=0,B=0)" — 红色阵营）
 *   - TextureParameters: 纹理参数（如 "TeamIcon=Texture2D"）
 *   - TeamShortName:     阵营简称（如 "红队"/"蓝队"）
 *
 * 可一键 ApplyToActor / ApplyToMaterial / ApplyToMeshComponent 批量应用到模型。
 *
 * 蓝图用法：
 *   创建 DA_TeamRed → ColorParameters: {"TeamColor": Red, "EnemyColor": Blue}
 *   阵营创建时引用此资产，自动应用到所有阵营成员的材质。
 */
UCLASS(BlueprintType)
class UDark_TdoreTeamDisplayAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 标量参数映射（如浮点值参数） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, float> ScalarParameters;

	/** 颜色参数映射（如阵营主色、副色） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, FLinearColor> ColorParameters;

	/** 纹理参数映射（如图标纹理、旗帜纹理） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, TObjectPtr<UTexture>> TextureParameters;

	/** 阵营短名称（显示在 UI 上） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText TeamShortName;

public:
	/** 将参数应用到材质实例（动态材质） */
	UFUNCTION(BlueprintCallable, Category = Teams)
	void ApplyToMaterial(UMaterialInstanceDynamic* Material);

	/** 将参数应用到网格组件的所有材质 */
	UFUNCTION(BlueprintCallable, Category = Teams)
	void ApplyToMeshComponent(UMeshComponent* MeshComponent);

	/** 将参数应用到 Niagara 粒子组件 */
	UFUNCTION(BlueprintCallable, Category = Teams)
	void ApplyToNiagaraComponent(UNiagaraComponent* NiagaraComponent);

	/** 一键应用到 Actor 的所有 MeshComponent + NiagaraComponent */
	UFUNCTION(BlueprintCallable, Category = Teams, meta = (DefaultToSelf = "TargetActor"))
	void ApplyToActor(AActor* TargetActor, bool bIncludeChildActors = true);
};
