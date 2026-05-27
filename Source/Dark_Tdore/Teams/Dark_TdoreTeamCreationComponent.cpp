// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTeamCreationComponent.h"
#include "Dark_TdoreTeamPublicInfo.h"
#include "Dark_TdoreTeamSubsystem.h"
#include "Dark_TdoreTeamDisplayAsset.h"
#include "Dark_TdoreTeamAgentInterface.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Player/Dark_TdorePlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreTeamCreationComponent)

// ============ 构造 ============

UDark_TdoreTeamCreationComponent::UDark_TdoreTeamCreationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 默认使用 C++ 基类作为 PublicInfo 类型
	// 蓝图可覆写为子类（如 BP_TeamPublicInfo）
	PublicTeamInfoClass = ADark_TdoreTeamPublicInfo::StaticClass();
}

// ============ BeginPlay — 自动创建阵营 + 分配玩家 ============

void UDark_TdoreTeamCreationComponent::BeginPlay()
{
	Super::BeginPlay();

	// -----------------------------------------------------------------
	// Step 1: 创建配置的阵营
	//   遍历 TeamsToCreate Map，每个条目调用 CreateTeam 生成一个
	//   PublicInfo Actor → 注册到 TeamSubsystem
	// -----------------------------------------------------------------
	for (const auto& KVP : TeamsToCreate)
	{
		CreateTeam(KVP.Key, KVP.Value);
	}

	// -----------------------------------------------------------------
	// Step 2: 分配已有玩家到人数最少的阵营
	//   逻辑：遍历所有 PlayerState → 找到人数最少的阵营 → 分配
	//   目的：保证双方人数尽量均衡
	// -----------------------------------------------------------------
	if (HasAuthority())
	{
		AGameStateBase* GameState = GetGameStateChecked<AGameStateBase>();
		UDark_TdoreTeamSubsystem* TeamSub = GetWorld()->GetSubsystem<UDark_TdoreTeamSubsystem>();
		check(TeamSub);

		for (APlayerState* PS : GameState->PlayerArray)
		{
			if (ADark_TdorePlayerState* DarkPS = Cast<ADark_TdorePlayerState>(PS))
			{
				if (IDark_TdoreTeamAgentInterface* TeamAgent = Cast<IDark_TdoreTeamAgentInterface>(DarkPS))
				{
					// 找到当前人数最少的阵营
					int32 BestTeamId = INDEX_NONE;
					int32 BestCount = TNumericLimits<int32>::Max();

					for (int32 TeamId : TeamSub->GetTeamIDs())
					{
						int32 Count = 0;
						for (APlayerState* OtherPS : GameState->PlayerArray)
						{
							if (ADark_TdorePlayerState* OtherDarkPS = Cast<ADark_TdorePlayerState>(OtherPS))
							{
								int32 OtherTeam = TeamSub->FindTeamFromObject(OtherDarkPS);
								if (OtherTeam == TeamId) Count++;
							}
						}
						if (Count < BestCount) { BestCount = Count; BestTeamId = TeamId; }
					}

					// 分配阵营
					if (BestTeamId != INDEX_NONE)
					{
						TeamAgent->SetGenericTeamId(IntegerToGenericTeamId(BestTeamId));
					}
				}
			}
		}
	}
}

// ============ CreateTeam — 创建一个阵营 Actor ============

void UDark_TdoreTeamCreationComponent::CreateTeam(int32 TeamId, UDark_TdoreTeamDisplayAsset* DisplayAsset)
{
	check(HasAuthority());  // 仅服务器可创建

	UWorld* World = GetWorld();
	check(World);

	// 配置生成参数：始终生成，无碰撞检查
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Step 1: 生成 PublicInfo Actor → SetTeamId → SetTeamDisplayAsset
	// Step 2: Actor BeginPlay → TryRegisterWithTeamSubsystem（自动注册）
	ADark_TdoreTeamPublicInfo* NewPublicInfo = World->SpawnActor<ADark_TdoreTeamPublicInfo>(
		PublicTeamInfoClass, SpawnInfo);

	checkf(NewPublicInfo, TEXT("创建阵营 %d PublicInfo 失败: 蓝图类=%s"),
		TeamId, *GetPathNameSafe(*PublicTeamInfoClass));

	NewPublicInfo->SetTeamId(TeamId);
	NewPublicInfo->SetTeamDisplayAsset(DisplayAsset);
}
