// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreAnimInstance.h"
#include "AbilitySystemGlobals.h"
#include "Character/Dark_TdoreCharacterMovementComponent.h"
#include "Dark_TdoreCharacter.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreAnimInstance)

UDark_TdoreAnimInstance::UDark_TdoreAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDark_TdoreAnimInstance::InitializeWithAbilitySystem(UAbilitySystemComponent* ASC)
{
	check(ASC);

	// 绑定 GameplayTagPropertyMap 到 ASC
	// 此后动画蓝图中映射的蓝图变量将随 ASC 标签自动更新
	GameplayTagPropertyMap.Initialize(this, ASC);
}

#if WITH_EDITOR
EDataValidationResult UDark_TdoreAnimInstance::IsDataValid(FDataValidationContext& Context) const
{
	Super::IsDataValid(Context);

	GameplayTagPropertyMap.IsDataValid(this, Context);

	return ((Context.GetNumErrors() > 0) ? EDataValidationResult::Invalid : EDataValidationResult::Valid);
}
#endif

void UDark_TdoreAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// 自动查找 OwningActor 的 ASC 并初始化 Tag 映射
	if (AActor* OwningActor = GetOwningActor())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor))
		{
			InitializeWithAbilitySystem(ASC);
		}
	}
}

void UDark_TdoreAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// 获取地面距离：供动画蓝图中的 FootIK 和其他后处理使用
	const ADark_TdoreCharacter* Character = Cast<ADark_TdoreCharacter>(GetOwningActor());
	if (!Character)
	{
		return;
	}

	// 参考 Lyra：GetGroundInfo() 非 const，所以不能用 const 指针
	UDark_TdoreCharacterMovementComponent* CharMoveComp = CastChecked<UDark_TdoreCharacterMovementComponent>(Character->GetCharacterMovement());
	const FDark_TdoreCharacterGroundInfo& GroundInfo = CharMoveComp->GetGroundInfo();
	GroundDistance = GroundInfo.GroundDistance;
}
