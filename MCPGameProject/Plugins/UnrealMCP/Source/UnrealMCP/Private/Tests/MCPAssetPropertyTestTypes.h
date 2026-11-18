#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Misc/DataValidation.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "MCPAssetPropertyTestTypes.generated.h"

USTRUCT()
struct FMCPDataTableTestRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Test", meta=(ClampMin="1"))
    int32 Count = 1;
    UPROPERTY(EditAnywhere, Category="Test", meta=(ClampMin="0"))
    float Delay = 0;
    UPROPERTY(EditAnywhere, Category="Test")
    TSoftClassPtr<AActor> Enemy;
    UPROPERTY(EditAnywhere, Category="Test")
    FText Label;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override
    {
        if (Count < 1 || Delay < 0) { Context.AddError(NSLOCTEXT("MCPDataTableTest", "Invalid", "Invalid row count/delay")); return EDataValidationResult::Invalid; }
        return EDataValidationResult::Valid;
    }
#endif
};

UCLASS(NotBlueprintable, Hidden, meta=(MCPAnimationNotifyAuthoringSafe="true"))
class UMCPAnimationTestNotify : public UAnimNotify
{
    GENERATED_BODY()
public:
    UMCPAnimationTestNotify() { bIsNativeBranchingPoint = true; bShouldFireInEditor = false; }
    static inline int32 CallCount = 0;
    static inline int32 LastMontageInstance = INDEX_NONE;
    static inline const UAnimSequenceBase* LastAnimation = nullptr;
    virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override
    { ++CallCount; LastMontageInstance = Payload.MontageInstanceID; LastAnimation = Payload.SequenceAsset; }
};

USTRUCT()
struct FMCPAnimationTestAppearance
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Test")
    TArray<FLinearColor> Colors;
    UPROPERTY(EditAnywhere, Category="Test")
    FName SizeParameter;
    UPROPERTY(EditAnywhere, Category="Test", meta=(ClampMin="0.01"))
    float SizeMultiplier = 1;
    UPROPERTY(EditAnywhere, Category="Test")
    FName BrightnessParameter;
    UPROPERTY(EditAnywhere, Category="Test")
    float BrightnessMultiplier = 3;
};

UCLASS(NotBlueprintable, Hidden, meta=(MCPNiagaraAppearanceNotifyAuthoringSafe="true"))
class UMCPAnimationAppearanceTestNotify : public UAnimNotify
{
    GENERATED_BODY()
public:
    UMCPAnimationAppearanceTestNotify() { bIsNativeBranchingPoint = true; bShouldFireInEditor = false; }
    UPROPERTY(EditAnywhere, Category="Test")
    TObjectPtr<UNiagaraSystem> Template;
    UPROPERTY(EditAnywhere, Category="Test")
    FName SocketName;
    UPROPERTY(EditAnywhere, Category="Test")
    bool bAttached = true;
    UPROPERTY(EditAnywhere, Category="Test")
    bool bAbsoluteScale = false;
    UPROPERTY(EditAnywhere, Category="Test")
    FVector LocationOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category="Test")
    FRotator RotationOffset = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, Category="Test", meta=(ClampMin="0.01"))
    FVector Scale = FVector::OneVector;
    UPROPERTY(EditAnywhere, Category="Test")
    FMCPAnimationTestAppearance Appearance;
    UPROPERTY()
    int32 HiddenSentinel = 73;
    static inline int32 CallCount = 0;
    static inline float DeliveredSize = 0;
    virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override;
};

USTRUCT()
struct FMCPAssetPropertyTestStats
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Test", meta=(ClampMin="0", ClampMax="1000"))
    int32 Health = 100;

    UPROPERTY(EditAnywhere, Category="Test")
    FVector Offset = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FMCPAssetPropertyTestBulletHits
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Test")
    bool bEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Test", meta=(EditCondition="bEnabled"))
    TArray<FName> HeadRootBones;
};

USTRUCT()
struct FMCPImpactTestBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Test")
    TObjectPtr<UNiagaraSystem> System;
    UPROPERTY(EditAnywhere, Category="Test")
    TObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere, Category="Test", meta=(ClampMin="0.01"))
    float Scale = 1;
};

USTRUCT()
struct FMCPImpactTestDecal
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Test")
    TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, Category="Test")
    FVector Size = FVector(2, 5, 5);
    UPROPERTY(EditAnywhere, Category="Test")
    float HoldTime = 10;
    UPROPERTY(EditAnywhere, Category="Test")
    float FadeTime = 2;
};

USTRUCT()
struct FMCPImpactTestSurface : public FMCPImpactTestBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Test")
    FMCPImpactTestDecal Decal;
};

UCLASS(NotBlueprintable, Hidden)
class UMCPImpactTestProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Test")
    FMCPImpactTestSurface Default;
    UPROPERTY(EditDefaultsOnly, Category="Test")
    TMap<TEnumAsByte<EPhysicalSurface>, FMCPImpactTestSurface> Surfaces;
};

USTRUCT(BlueprintType)
struct FMCPAssetPropertyTestUpgrade
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Test", meta=(ClampMin="0"))
    int32 Cost = 0;
    UPROPERTY()
    float DamageMultiplier = 1;
    UPROPERTY()
    float FireRateMultiplier = 1;
};

USTRUCT()
struct FMCPAssetTextTestBranch
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Test")
    FText Name;
    UPROPERTY(EditAnywhere, Category="Test")
    FText Description;
    UPROPERTY(EditAnywhere, Category="Test")
    int32 Cost = 17;
};

USTRUCT(BlueprintType)
struct FMCPPropertyPathVortex
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Test")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Test", meta=(ClampMin="0", ClampMax="1", EditCondition="bEnabled"))
    float PullOuterRadiusFraction = 1.0f / 3.0f;
    UPROPERTY(EditAnywhere, Category="Test")
    float PullPeakRadiusFraction = 0.3f;
    UPROPERTY(EditAnywhere, Category="Test")
    float CoreSlowRadiusFraction = 0.5f;
    UPROPERTY()
    float Hidden = 0.123456791f;
};

USTRUCT(BlueprintType)
struct FMCPPropertyPathStage
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Test")
    FMCPPropertyPathVortex Vortex;
    UPROPERTY(EditAnywhere, Category="Test")
    TArray<FMCPPropertyPathVortex> Samples;
};

USTRUCT(BlueprintType)
struct FMCPPropertyPathTable
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Test")
    TMap<int32, FMCPPropertyPathStage> A;
    UPROPERTY(EditAnywhere, Category="Test")
    TMap<FName, FMCPPropertyPathStage> Names;
    UPROPERTY(EditAnywhere, Category="Test")
    TMap<FString, FMCPPropertyPathStage> Labels;
    UPROPERTY(EditAnywhere, Category="Test")
    TMap<TEnumAsByte<EPhysicalSurface>, FMCPPropertyPathStage> Surfaces;
    UPROPERTY(EditAnywhere, Category="Test")
    TArray<FMCPPropertyPathStage> Rows;

    FMCPPropertyPathTable()
    {
        A.Add(3); A.Add(4); A.Add(5);
        A[4].Vortex.PullOuterRadiusFraction = 0.123456791f;
        A[5].Vortex.PullOuterRadiusFraction = 0.987654328f;
        A[3].Samples.AddDefaulted();
        Names.Add(TEXT("Named")); Labels.Add(TEXT("literal.key[3]")); Surfaces.Add(SurfaceType1);
        Rows.AddDefaulted(); Rows[0].Samples.AddDefaulted();
    }
};

UCLASS(Blueprintable, BlueprintType)
class UMCPPropertyPathModel : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Test")
    FMCPPropertyPathTable Stages;
    UPROPERTY(EditDefaultsOnly, Category="Test")
    TObjectPtr<UTexture2D> Texture;
};

UCLASS(NotBlueprintable, Hidden)
class UMCPAssetPropertyTestAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Test")
    FMCPPropertyPathTable Stages;

    UPROPERTY(EditAnywhere, Category="Test")
    FText DisplayName;

    UPROPERTY(EditAnywhere, Category="Test")
    FMCPAssetTextTestBranch TextBranch;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Test")
    FMCPAssetPropertyTestBulletHits BulletHits;

    UPROPERTY(EditDefaultsOnly, Category="Test")
    TArray<FMCPAssetPropertyTestBulletHits> HitGroups;

    UPROPERTY(EditDefaultsOnly, Category="Test")
    TArray<FMCPAssetPropertyTestUpgrade> UpgradeStages;

    UPROPERTY(EditDefaultsOnly, Category="Test", meta=(EditCondition="Enabled"))
    FMCPAssetPropertyTestStats ConditionalStats;

    UPROPERTY(EditDefaultsOnly, Category="Test", meta=(EditCondition="!Enabled"))
    int32 NegatedValue = 0;

    UPROPERTY(EditAnywhere, Category="Test", meta=(EditCondition="Enabled && Byte > 0"))
    int32 ComplexValue = 0;

    UPROPERTY(EditAnywhere, Category="Test", meta=(EditCondition="UnknownToggle"))
    int32 UnknownConditionValue = 0;

    UPROPERTY(EditAnywhere, Category="Test")
    FMCPAssetPropertyTestStats Stats;

    UPROPERTY(EditAnywhere, Category="Test")
    TArray<FMCPAssetPropertyTestStats> Attacks;

    UPROPERTY(EditAnywhere, Category="Test")
    TArray<int32> Numbers;

    UPROPERTY(EditAnywhere, Category="Test")
    TObjectPtr<UTexture2D> Texture;

    UPROPERTY(EditAnywhere, Category="Test")
    TSoftObjectPtr<UTexture2D> SoftTexture;

    UPROPERTY(EditAnywhere, Category="Test")
    TSubclassOf<AActor> ActorClass;

    UPROPERTY(EditAnywhere, Category="Test")
    TSoftClassPtr<AActor> SoftActorClass;

    UPROPERTY(EditAnywhere, Category="Test")
    FGameplayTag Tag;

    UPROPERTY(EditAnywhere, Category="Test")
    FGameplayTagContainer Tags;

    UPROPERTY(EditAnywhere, Category="Test")
    FString Label;

    UPROPERTY(EditAnywhere, Category="Test")
    float Scale = 1;

    UPROPERTY(EditAnywhere, Category="Test")
    uint8 Byte = 0;

    UPROPERTY(EditAnywhere, Category="Test")
    bool Enabled = true;

    UPROPERTY(EditAnywhere, Category="Test")
    TEnumAsByte<EAutoReceiveInput::Type> Input = EAutoReceiveInput::Disabled;

    UPROPERTY(VisibleAnywhere, Category="Test")
    int32 ReadOnly = 7;

    UPROPERTY(EditAnywhere, Category="Test")
    TSet<FName> UnsupportedMap;
};