// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dark_TdoreWeaponActor.generated.h"

class APawn;
class UDark_TdoreEquipmentInstance;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Base visible weapon actor spawned by EquipmentDefinition.ActorsToSpawn.
 *
 * This mirrors Lyra's BP weapon actor pattern: the actor owns presentation
 * components, while UDark_TdoreWeaponInstance owns runtime equipment logic.
 */
UCLASS(Blueprintable, BlueprintType)
class ADark_TdoreWeaponActor : public AActor
{
	GENERATED_BODY()

public:
	ADark_TdoreWeaponActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;

	/** Called by the equipment instance immediately after this actor is spawned and attached. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitializeFromEquipmentInstance(UDark_TdoreEquipmentInstance* InEquipmentInstance);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UDark_TdoreEquipmentInstance* GetOwningEquipmentInstance() const { return OwningEquipmentInstance; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	APawn* GetOwningPawn() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UStaticMeshComponent* GetWeaponMeshComponent() const { return WeaponMeshComponent; }

	/** Useful for melee traces or debug pickup previews. Defaults to off for equipped presentation actors. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetWeaponCollisionEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetWeaponVisible(bool bVisible);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon", meta = (DisplayName = "On Initialized From Equipment Instance"))
	void K2_OnInitializedFromEquipmentInstance(UDark_TdoreEquipmentInstance* InEquipmentInstance);

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> WeaponRootComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> WeaponMeshComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	bool bDisableCollisionOnBeginPlay = true;

	UPROPERTY(Transient)
	TObjectPtr<UDark_TdoreEquipmentInstance> OwningEquipmentInstance;
};
