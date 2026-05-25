---
name: lyra-references
description: >
  Lyra Starter Game architecture reference for UE5 projects. This skill should be used when the user asks about
  implementing patterns from Lyra (Experience system, GAS integration, modular GameFeatures, InitState patterns,
  Enhanced Input with GAS, CommonUI, Equipment/Inventory systems, Teams, or Verb messages). Also use when the
  user wants to replicate Lyra-style architecture in their own UE5 project. The skill contains a comprehensive
  architecture document and can search the Lyra source code at C:\Users\dell\Documents\Unreal Projects\LyraStarterGame\Source\.
---

# Lyra Starter Game References

## Overview

This skill provides architectural patterns and code references from Epic's Lyra Starter Game (UE 5.x).
Lyra is the gold standard for modular UE5 project architecture, demonstrating GAS, EnhancedInput,
ModularGameplay, GameFeatures, CommonUI, and more.

## When to Use

- User asks "how does Lyra do X?" (input, GAS, Experience, equipment, UI, etc.)
- User wants to implement a modular game architecture
- User asks about GAS + Enhanced Input integration patterns
- User wants to understand GameFeature plugin architecture
- User asks about InitState / PawnExtension pattern
- User wants to replicate Lyra-style systems in their project

## How to Use This Skill

### Step 1: Read the Architecture Document First

The reference file `references/lyra-architecture.md` contains a comprehensive overview of all Lyra systems.
Read it first to understand which pattern applies to the user's task.

### Step 2: Search Lyra Source for Specific Patterns

The Lyra source is at:
```
C:\Users\dell\Documents\Unreal Projects\LyraStarterGame\Source\LyraGame\
```

Use `search_content` to search the Lyra source directory for specific patterns:
- Search for class names: `LyraExperienceDefinition`, `LyraAbilitySet`, `LyraHeroComponent`
- Search for patterns: `IGameFrameworkInitStateInterface`, `GameFeatureAction`, `AbilityInputTag`
- Search for GAS patterns: `ActivationGroup`, `AbilityCost`, `LyraAbilitySystemComponent`

### Step 3: Read Specific Lyra Source Files

When implementing a specific system, read the relevant Lyra source files directly:

**Experience System:**
- `GameModes/LyraExperienceDefinition.h`
- `GameModes/LyraExperienceManagerComponent.h`
- `GameModes/LyraGameMode.h`

**GAS Integration:**
- `AbilitySystem/LyraGameplayAbility.h` + `.cpp`
- `AbilitySystem/LyraAbilitySystemComponent.h` + `.cpp`
- `AbilitySystem/LyraAbilitySet.h` + `.cpp`
- `AbilitySystem/Attributes/LyraHealthSet.h` + `.cpp`

**Character/Pawn:**
- `Character/LyraPawnExtensionComponent.h` + `.cpp`
- `Character/LyraHeroComponent.h` + `.cpp`
- `Character/LyraCharacter.h` + `.cpp`
- `Character/LyraPawnData.h`

**GameFeatures:**
- `GameFeatures/LyraGameFeaturePolicy.h`
- `GameFeatures/GameFeatureAction_WorldActionBase.h`
- `GameFeatures/GameFeatureAction_AddAbilities.h` + `.cpp`

**Equipment/Inventory:**
- `Equipment/LyraEquipmentDefinition.h`
- `Equipment/LyraEquipmentManagerComponent.h` + `.cpp`
- `Inventory/LyraInventoryManagerComponent.h` + `.cpp`

**UI:**
- `UI/LyraHUDLayout.h`
- `UI/IndicatorSystem/LyraIndicatorManagerComponent.h`
- `UI/Frontend/LyraFrontendStateComponent.h`

**Teams:**
- `Teams/LyraTeamSubsystem.h` + `.cpp`
- `Teams/LyraTeamDisplayAsset.h`

### Step 4: Show Code Patterns as Examples

When generating code for the user's project, include comments referencing the Lyra pattern used:
```cpp
// Pattern from Lyra: Experience-driven modular game mode
// Uses ULyraExperienceDefinition + ModularGameplayActors
```

## Key Architecture Patterns to Reference

### 1. Experience-Driven Game Loading
Instead of traditional GameMode overrides, use a DataAsset (`ULyraExperienceDefinition`) that:
- Lists GameFeature plugins to enable
- Defines GameFeatureAction objects to execute
- Can be combined with ActionSets for reuse

### 2. InitState Component Coordination
Use `IGameFrameworkInitStateInterface` on components to coordinate startup:
- `ULyraPawnExtensionComponent` acts as the init coordinator
- Components register with `RegisterAndCallForInitStateChange`
- Order: Spawned → DataAvailable → DataInitialized → GameplayReady

### 3. GAS + Enhanced Input via GameplayTags
- `ULyraInputConfig` maps `UInputAction` → `FGameplayTag`
- `ULyraHeroComponent::Input_AbilityInputTagPressed()` routes to ASC
- ASC uses `AbilityInputTagPressed/Released()` with input buffering
- Abilities declare `AbilityTriggers` with `InputTag` matching

### 4. Modular GameMode/GameState
- Use `AModularGameModeBase` / `AModularGameStateBase` from ModularGameplay plugin
- Components are added to GameState, not monolithic overrides
- Experience loading is a GameStateComponent

### 5. AbilitySet Data Assets
- Bundle ability grants + attribute sets + gameplay effects into a DataAsset
- `ULyraAbilitySet::GiveToAbilitySystem()` grants all at once
- Supports revoke via `FLyraAbilitySet_GrantedHandles`

### 6. GameFeature Actions for Cross-Plugin Integration
- Plugins don't directly reference each other
- GameFeatureAction objects bridge plugins when features activate
- `GameFeatureAction_AddAbilities` grants abilities to matching actors in the world

## Common Module Dependencies (Lyra-style)

When the user asks about Lyra-style module setup, reference these dependencies:
```
Public: Core, CoreUObject, Engine, GameplayTags, GameplayTasks,
        GameplayAbilities, AIModule, ModularGameplay, ModularGameplayActors,
        DataRegistry, ReplicationGraph, GameFeatures, SignificanceManager,
        Hotfix, CommonLoadingScreen, Niagara
Private: EnhancedInput, CommonUI, CommonInput, CommonGame, CommonUser,
         GameSettings, GameplayMessageRuntime, UIExtension, GameSubtitles
```
