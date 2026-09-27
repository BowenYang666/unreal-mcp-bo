#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInterface.h"
#include "MCPAssetPropertyTestTypes.generated.h"

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

UCLASS(NotBlueprintable, Hidden)
class UMCPAssetPropertyTestAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Test")
    FMCPAssetPropertyTestBulletHits BulletHits;

    UPROPERTY(EditDefaultsOnly, Category="Test")
    TArray<FMCPAssetPropertyTestBulletHits> HitGroups;

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