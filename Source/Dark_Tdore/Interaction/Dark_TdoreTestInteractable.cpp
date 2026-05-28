// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreTestInteractable.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "Components/SphereComponent.h"

ADark_TdoreTestInteractable::ADark_TdoreTestInteractable()
{
	PrimaryActorTick.bCanEverTick = false;

	// 碰撞球体：让 GrantNearbyInteraction 的 OverlapMultiByChannel 能扫到
	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->SetSphereRadius(50.0f);
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	SetRootComponent(SphereCollision);
}

void ADark_TdoreTestInteractable::GatherInteractionOptions(const FDark_TdoreInteractionQuery& InteractQuery, FInteractionOptionBuilder& OptionBuilder)
{
	FDark_TdoreInteractionOption Option;
	Option.Text = InteractionText;
	Option.InteractionAbilityToGrant = InteractionAbilityClass;
	OptionBuilder.AddInteractionOption(Option);

	UE_LOG(LogDark_TdoreGAS, Display, TEXT("[Interaction] TestInteractable '%s' \u88AB\u626B\u63CF\u5230\uFF0C\u8FD4\u56DE\u9009\u9879: '%s'"),
		*GetName(), *InteractionText.ToString());
}
