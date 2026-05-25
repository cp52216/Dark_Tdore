// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdorePawnExtensionComponent.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Dark_TdoreAbilityTagRelationshipMapping.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdorePawnExtensionComponent)

class UActorComponent;

// 本特征在 GameFrameworkComponentManager 中的注册名
// 其他组件通过此名称声明对 PawnExtensionComponent 的初始化依赖
const FName UDark_TdorePawnExtensionComponent::NAME_ActorFeatureName("PawnExtension");

UDark_TdorePawnExtensionComponent::UDark_TdorePawnExtensionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 不需要 Tick — 完全由 InitState 事件驱动
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);

	AbilitySystemComponent = nullptr;
}

// 组件注册时：验证只附加在 Pawn 上，且全局唯一
// 参考 Lyra：RegisterInitStateFeature 向 GameFrameworkComponentManager 注册本特征
void UDark_TdorePawnExtensionComponent::OnRegister()
{
	Super::OnRegister();

	const APawn* Pawn = GetPawn<APawn>();
	ensureAlwaysMsgf((Pawn != nullptr), TEXT("Dark_TdorePawnExtensionComponent on [%s] can only be added to Pawn actors."), *GetNameSafe(GetOwner()));

	TArray<UActorComponent*> PawnExtensionComponents;
	Pawn->GetComponents(UDark_TdorePawnExtensionComponent::StaticClass(), PawnExtensionComponents);
	ensureAlwaysMsgf((PawnExtensionComponents.Num() == 1), TEXT("Only one Dark_TdorePawnExtensionComponent should exist on [%s]."), *GetNameSafe(GetOwner()));

	// 向 GameFrameworkComponentManager 注册 InitState 特征（仅在游戏世界有效）
	RegisterInitStateFeature();
}

// BeginPlay：绑定 InitState 监听 → 推进到 Spawned → 尝试继续推进
// 参考 Lyra：这是 InitState 状态机的入口
void UDark_TdorePawnExtensionComponent::BeginPlay()
{
	Super::BeginPlay();

	// 监听同一 Actor 上所有特征的 InitState 变化（NAME_None = 监听所有）
	BindOnActorInitStateChanged(NAME_None, FGameplayTag(), false);

	// 推进到 InitState.Spawned，然后 CheckDefaultInitialization 尝试推进到后续状态
	ensure(TryToChangeInitState(FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned"))));
	CheckDefaultInitialization();
}

// EndPlay：清理 ASC 关联 → 注销 InitState 特征
void UDark_TdorePawnExtensionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeAbilitySystem();
	UnregisterInitStateFeature();

	Super::EndPlay(EndPlayReason);
}

// 参考 Lyra ULyraPawnExtensionComponent：设置 ASC 与 Pawn 的 Avatar 关联
// 1. 如果已有旧 ASC，先解除
// 2. 如果已有旧 Avatar Pawn（网络延迟导致），先踢掉
// 3. 设置新的 ASC → InitAbilityActorInfo(OwnerActor, Pawn)
// 4. 广播 OnAbilitySystemInitialized 委托（HealthComponent 等监听此委托进行子初始化）
void UDark_TdorePawnExtensionComponent::InitializeAbilitySystem(UDark_TdoreAbilitySystemComponent* InASC, AActor* InOwnerActor)
{
	check(InASC);
	check(InOwnerActor);

	if (AbilitySystemComponent == InASC)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		UninitializeAbilitySystem();
	}

	APawn* Pawn = GetPawnChecked<APawn>();
	AActor* ExistingAvatar = InASC->GetAvatarActor();

	UE_LOG(LogDark_TdoreGAS, Verbose, TEXT("Setting up ASC [%s] on pawn [%s] owner [%s], existing [%s]"),
		*GetNameSafe(InASC), *GetNameSafe(Pawn), *GetNameSafe(InOwnerActor), *GetNameSafe(ExistingAvatar));

	// 处理网络延迟：旧 Pawn 可能还没被正确清理，先解除它的 Avatar 关联
	if ((ExistingAvatar != nullptr) && (ExistingAvatar != Pawn))
	{
		UE_LOG(LogDark_TdoreGAS, Log, TEXT("Existing avatar (authority=%d)"), ExistingAvatar->HasAuthority() ? 1 : 0);

		ensure(!ExistingAvatar->HasAuthority());

		if (UDark_TdorePawnExtensionComponent* OtherExtensionComponent = FindPawnExtensionComponent(ExistingAvatar))
		{
			OtherExtensionComponent->UninitializeAbilitySystem();
		}
	}

	AbilitySystemComponent = InASC;
	AbilitySystemComponent->InitAbilityActorInfo(InOwnerActor, Pawn);

	// 广播 ASC 初始化完成 — 其他组件监听此委托进行各自的子初始化
	OnAbilitySystemInitialized.Broadcast();
}

// 在 ASC 初始化完成后调用，将蓝图配置的 TagRelationshipMapping DataAsset 设置到 ASC。
// 此后技能激活/Block/Cancel 时 ASC 会自动查询此映射表，扩展 Block 和 Cancel 标签。
// 参考 Lyra：LyraPawnExtensionComponent::InitializeAbilitySystem 中同样调用 SetTagRelationshipMapping
void UDark_TdorePawnExtensionComponent::SetTagRelationshipMapping(UDark_TdoreAbilityTagRelationshipMapping* NewMapping)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetTagRelationshipMapping(NewMapping);
	}
}

// 解除 ASC 的 Avatar 关联
// 1. 取消所有技能（排除 SurvivesDeath 类型的技能）
// 2. 清除输入缓存
// 3. 移除 GameplayCue
// 4. 清除 Avatar 引用
void UDark_TdorePawnExtensionComponent::UninitializeAbilitySystem()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	if (AbilitySystemComponent->GetAvatarActor() == GetOwner())
	{
		AbilitySystemComponent->CancelAbilities(nullptr, nullptr);
		AbilitySystemComponent->ClearAbilityInput();
		AbilitySystemComponent->RemoveAllGameplayCues();

		if (AbilitySystemComponent->GetOwnerActor() != nullptr)
		{
			AbilitySystemComponent->SetAvatarActor(nullptr);
		}
		else
		{
			AbilitySystemComponent->ClearActorInfo();
		}

		OnAbilitySystemUninitialized.Broadcast();
	}

	AbilitySystemComponent = nullptr;
}

// Pawn 的 Controller 变化时调用（Possess/UnPossess）
// 1. 刷新 ASC 的 AbilityActorInfo（Owner 可能变了）
// 2. 推进 InitState（可能有新的条件满足）
void UDark_TdorePawnExtensionComponent::HandleControllerChanged()
{
	if (AbilitySystemComponent && (AbilitySystemComponent->GetAvatarActor() == GetPawnChecked<APawn>()))
	{
		ensure(AbilitySystemComponent->AbilityActorInfo->OwnerActor == AbilitySystemComponent->GetOwnerActor());
		if (AbilitySystemComponent->GetOwnerActor() == nullptr)
		{
			UninitializeAbilitySystem();
		}
		else
		{
			AbilitySystemComponent->RefreshAbilityActorInfo();
		}
	}

	CheckDefaultInitialization();
}

// PlayerState 网络复制完成时调用 → 尝试推进 InitState
void UDark_TdorePawnExtensionComponent::HandlePlayerStateReplicated()
{
	CheckDefaultInitialization();
}

// InputComponent 设置完成时调用 → 尝试推进 InitState
void UDark_TdorePawnExtensionComponent::SetupPlayerInputComponent()
{
	CheckDefaultInitialization();
}

// 检查并推进 InitState 链：Spawned → DataAvailable → DataInitialized → GameplayReady
// 参考 Lyra：先推进其他实现者的状态，再推进自己的状态
void UDark_TdorePawnExtensionComponent::CheckDefaultInitialization()
{
	static const TArray<FGameplayTag> StateChain = {
		FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned")),
		FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable")),
		FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized")),
		FGameplayTag::RequestGameplayTag(TEXT("InitState.GameplayReady"))
	};

	// 先检查/推进其他实现者（如 HeroComponent 等）
	CheckDefaultInitializationForImplementers();
	// 然后推进自己的状态
	ContinueInitStateChain(StateChain);
}

// InitState 转换条件判断（参考 Lyra 状态机逻辑）
bool UDark_TdorePawnExtensionComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
	check(Manager);

	APawn* Pawn = GetPawn<APawn>();
	static const FGameplayTag Spawned = FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned"));
	static const FGameplayTag DataAvailable = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable"));
	static const FGameplayTag DataInitialized = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized"));
	static const FGameplayTag GameplayReady = FGameplayTag::RequestGameplayTag(TEXT("InitState.GameplayReady"));

	// 从无状态 → Spawned：只要在有效 Pawn 上就可以
	if (!CurrentState.IsValid() && DesiredState == Spawned)
	{
		if (Pawn)
		{
			return true;
		}
	}
	// Spawned → DataAvailable：Authority 或 Local 控制的 Pawn 需要有 Controller
	if (CurrentState == Spawned && DesiredState == DataAvailable)
	{
		const bool bHasAuthority = Pawn->HasAuthority();
		const bool bIsLocallyControlled = Pawn->IsLocallyControlled();

		if (bHasAuthority || bIsLocallyControlled)
		{
			if (!GetController<AController>())
			{
				return false;
			}
		}

		return true;
	}
	// DataAvailable → DataInitialized：所有特征都到达 DataAvailable 后才能推进
	else if (CurrentState == DataAvailable && DesiredState == DataInitialized)
	{
		return Manager->HaveAllFeaturesReachedInitState(Pawn, DataAvailable);
	}
	// DataInitialized → GameplayReady：立即允许
	else if (CurrentState == DataInitialized && DesiredState == GameplayReady)
	{
		return true;
	}

	return false;
}

// InitState 转换回调（当推进到指定状态时执行）
void UDark_TdorePawnExtensionComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	static const FGameplayTag DataInitialized = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized"));

	if (DesiredState == DataInitialized)
	{
		// 本组件进入 DataInitialized 后，其他组件（如 HeroComponent）监听此变化
		// 并执行各自的初始化逻辑（如绑定输入、设置摄像机等）
	}
}

// 监听同一 Actor 上其他特征的 InitState 变化
// 当其他特征到达 DataAvailable 时，重新检查本特征是否也可以推进
void UDark_TdorePawnExtensionComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	static const FGameplayTag DataAvailable = FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable"));

	if (Params.FeatureName != NAME_ActorFeatureName)
	{
		if (Params.FeatureState == DataAvailable)
		{
			CheckDefaultInitialization();
		}
	}
}

// 注册 ASC 初始化回调：如果 ASC 已就绪则立即调用，否则注册等待
// 这样无论注册时机如何，回调者都能收到通知
void UDark_TdorePawnExtensionComponent::OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate Delegate)
{
	if (!OnAbilitySystemInitialized.IsBoundToObject(Delegate.GetUObject()))
	{
		OnAbilitySystemInitialized.Add(Delegate);
	}

	if (AbilitySystemComponent)
	{
		Delegate.Execute();
	}
}

// 注册 ASC 解除初始化回调
void UDark_TdorePawnExtensionComponent::OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate Delegate)
{
	if (!OnAbilitySystemUninitialized.IsBoundToObject(Delegate.GetUObject()))
	{
		OnAbilitySystemUninitialized.Add(Delegate);
	}
}
