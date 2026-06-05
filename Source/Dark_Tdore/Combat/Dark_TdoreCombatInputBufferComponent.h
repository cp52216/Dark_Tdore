// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PawnComponent.h"
#include "GameplayTagContainer.h"
#include "Dark_TdoreCombatInputBufferComponent.generated.h"

USTRUCT(BlueprintType)
struct FDark_TdoreBufferedCombatInput
{
	GENERATED_BODY()

	// 被按下的输入标签，例如 InputTag.Ability.Attack.Light。
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag InputTag;

	// 输入发生时的世界时间，用于过期清理。
	UPROPERTY(BlueprintReadOnly)
	float TimeSeconds = 0.0f;
};

UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class UDark_TdoreCombatInputBufferComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UDark_TdoreCombatInputBufferComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// 记录一次输入。ASC 会在 AbilityInputTagPressed 入口自动调用它。
	UFUNCTION(BlueprintCallable, Category = "Combat|Input Buffer")
	void BufferInputTag(FGameplayTag InputTag);

	// 打开预输入消费窗口，通常由 AnimNotifyState 在连招可派生帧调用。
	UFUNCTION(BlueprintCallable, Category = "Combat|Input Buffer")
	void OpenInputBufferWindow(FName WindowName);

	// 关闭预输入消费窗口。使用计数而不是 bool，避免动画重叠窗口互相踩掉。
	UFUNCTION(BlueprintCallable, Category = "Combat|Input Buffer")
	void CloseInputBufferWindow(FName WindowName);

	// 在窗口打开时，从最新输入开始查找允许的标签，找到后立即消费。
	UFUNCTION(BlueprintCallable, Category = "Combat|Input Buffer")
	bool TryConsumeBufferedInput(const FGameplayTagContainer& AllowedInputTags, FGameplayTag& OutInputTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input Buffer")
	void ClearBufferedInputs();

	UFUNCTION(BlueprintPure, Category = "Combat|Input Buffer")
	bool IsInputBufferWindowOpen() const { return OpenWindowCount > 0; }

	UFUNCTION(BlueprintPure, Category = "Combat|Input Buffer")
	const TArray<FDark_TdoreBufferedCombatInput>& GetBufferedInputs() const { return BufferedInputs; }

private:
	void TrimExpiredInputs(float CurrentTime);

private:
	// 输入在缓冲区里最多保留多久。动作游戏常见范围 0.2 到 0.4 秒。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Input Buffer", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float BufferDuration = 0.35f;

	// 防止疯狂按键导致数组无限增长。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Input Buffer", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 MaxBufferedInputs = 4;

	UPROPERTY(Transient)
	TArray<FDark_TdoreBufferedCombatInput> BufferedInputs;

	UPROPERTY(Transient)
	int32 OpenWindowCount = 0;
};
