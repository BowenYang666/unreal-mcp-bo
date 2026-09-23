#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "EditorAssetLibrary.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Particles/Event/ParticleModuleEventGenerator.h"
#include "Distributions/Distribution.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
    class FCascadeReadContext
    {
    public:
        explicit FCascadeReadContext(UParticleSystem* InSystem) : System(InSystem) {}

        TSharedPtr<FJsonObject> Objects = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Assets = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Warnings;

        TSharedPtr<FJsonValue> Unavailable(const FString& Path, const FString& Reason)
        {
            const FString Message = Path + TEXT(": ") + Reason;
            if (!WarningSet.Contains(Message) && Warnings.Num() < 100)
            {
                WarningSet.Add(Message);
                Warnings.Add(MakeShared<FJsonValueString>(Message));
            }
            TSharedPtr<FJsonObject> Marker = MakeShared<FJsonObject>();
            Marker->SetStringField(TEXT("unavailable"), Reason);
            return MakeShared<FJsonValueObject>(Marker);
        }

        TSharedPtr<FJsonValue> ObjectValue(UObject* Object, int32 Depth)
        {
            if (!Object)
            {
                return MakeShared<FJsonValueNull>();
            }
            const FString Path = Object->GetPathName();
            TSharedPtr<FJsonObject> Reference = MakeShared<FJsonObject>();
            if (!Object->IsIn(System))
            {
                Reference->SetStringField(TEXT("asset_path"), Path);
                Reference->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
                Assets->SetObjectField(Path, Reference);
                return MakeShared<FJsonValueObject>(Reference);
            }
            Reference->SetStringField(TEXT("object_ref"), Path);
            if (!Objects->HasField(Path))
            {
                if (Depth > 24 || Objects->Values.Num() >= 2048)
                {
                    return Unavailable(Path, TEXT("Inline object depth/count limit reached"));
                }
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Objects->SetObjectField(Path, Entry);
                Entry->SetStringField(TEXT("name"), Object->GetName());
                Entry->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
                Entry->SetStringField(TEXT("display_name"), Object->GetClass()->GetDisplayNameText().ToString());
                Entry->SetStringField(TEXT("serialization"), TEXT("reflected_properties"));
                if (Object->IsA<UDistribution>())
                {
                    Entry->SetStringField(TEXT("kind"), TEXT("distribution"));
                    Entry->SetBoolField(TEXT("evaluated"), false);
                }
                else if (Object->IsA<UParticleModule>())
                {
                    Entry->SetStringField(TEXT("kind"), TEXT("module"));
                }
                if (!Object->GetClass()->GetPathName().StartsWith(TEXT("/Script/Engine.")))
                {
                    Unavailable(Path, TEXT("Custom class: reflected fields only; custom serialized data and runtime logic are not decoded"));
                }
                Entry->SetObjectField(TEXT("properties"), Properties(Object->GetClass(), Object, Path, Depth + 1));
            }
            return MakeShared<FJsonValueObject>(Reference);
        }

        TSharedPtr<FJsonObject> Properties(
            UStruct* Type, const void* Container, const FString& Path, int32 Depth,
            const TSet<FName>& Excluded = TSet<FName>())
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            for (TFieldIterator<FProperty> Property(Type); Property; ++Property)
            {
                if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
                    || Excluded.Contains(Property->GetFName()))
                {
                    continue;
                }
                const FString FieldPath = Path + TEXT(".") + Property->GetName();
                if (ValuesRead >= 100000)
                {
                    Result->SetField(TEXT("__remaining_fields"), Unavailable(FieldPath, TEXT("Property value budget reached")));
                    break;
                }
                if (Property->ArrayDim > 1)
                {
                    TArray<TSharedPtr<FJsonValue>> Values;
                    for (int32 Index = 0; Index < FMath::Min(Property->ArrayDim, 4096); ++Index)
                    {
                        Values.Add(Value(*Property, Property->ContainerPtrToValuePtr<void>(Container, Index), FieldPath, Depth));
                    }
                    if (Property->ArrayDim > 4096)
                    {
                        Values.Add(Unavailable(FieldPath, TEXT("Static array truncated at 4096 elements")));
                    }
                    Result->SetArrayField(Property->GetName(), Values);
                }
                else
                {
                    Result->SetField(Property->GetName(), Value(*Property,
                        Property->ContainerPtrToValuePtr<void>(Container), FieldPath, Depth));
                }
            }
            return Result;
        }

    private:
        UParticleSystem* System;
        int32 ValuesRead = 0;
        TSet<FString> WarningSet;

        TSharedPtr<FJsonValue> Value(FProperty* Property, const void* Address, const FString& Path, int32 Depth)
        {
            if (++ValuesRead > 100000 || Depth > 24)
            {
                return Unavailable(Path, TEXT("Property depth/value limit reached"));
            }
            if (const FBoolProperty* Boolean = CastField<FBoolProperty>(Property))
            {
                return MakeShared<FJsonValueBoolean>(Boolean->GetPropertyValue(Address));
            }
            if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
            {
                return MakeShared<FJsonValueString>(Enum->GetEnum()->GetNameStringByValue(
                    Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address)));
            }
            if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
            {
                return MakeShared<FJsonValueString>(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(Address)));
            }
            if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
            {
                if (Number->IsInteger())
                {
                    const FString Text = Number->GetNumericPropertyValueToString(Address);
                    const double NumericValue = FCString::Atod(*Text);
                    if (FMath::Abs(NumericValue) > 9007199254740991.0)
                    {
                        return MakeShared<FJsonValueString>(Text);
                    }
                    return MakeShared<FJsonValueNumber>(NumericValue);
                }
                const double NumberValue = Number->GetFloatingPointPropertyValue(Address);
                if (!FMath::IsFinite(NumberValue))
                {
                    return Unavailable(Path, TEXT("Non-finite number"));
                }
                return MakeShared<FJsonValueNumber>(NumberValue);
            }
            if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
            {
                const FString ReferencePath = Soft->GetPropertyValue(Address).ToString();
                TSharedPtr<FJsonObject> Reference = MakeShared<FJsonObject>();
                Reference->SetStringField(TEXT("asset_path"), ReferencePath);
                Reference->SetBoolField(TEXT("soft_reference"), true);
                if (!ReferencePath.IsEmpty())
                {
                    Assets->SetObjectField(ReferencePath, Reference);
                }
                return MakeShared<FJsonValueObject>(Reference);
            }
            if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
            {
                return ObjectValue(Object->GetObjectPropertyValue(Address), Depth + 1);
            }
            if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
            {
                TSharedPtr<FJsonObject> Fields = Properties(Struct->Struct, Address, Path, Depth + 1);
                Fields->SetStringField(TEXT("__struct"), Struct->Struct->GetName());
                return MakeShared<FJsonValueObject>(Fields);
            }
            if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
            {
                FScriptArrayHelper Helper(Array, Address);
                TArray<TSharedPtr<FJsonValue>> Values;
                const int32 Count = FMath::Min(Helper.Num(), 4096);
                for (int32 Index = 0; Index < Count && ValuesRead < 100000; ++Index)
                {
                    Values.Add(Value(Array->Inner, Helper.GetRawPtr(Index),
                        FString::Printf(TEXT("%s[%d]"), *Path, Index), Depth + 1));
                }
                if (Values.Num() != Helper.Num())
                {
                    Values.Add(Unavailable(Path, FString::Printf(TEXT("Array truncated; total_count=%d"), Helper.Num())));
                }
                return MakeShared<FJsonValueArray>(Values);
            }
            FString Text;
            Property->ExportTextItem_Direct(Text, Address, nullptr, nullptr, PPF_None);
            if (CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property) || CastField<FTextProperty>(Property))
            {
                return MakeShared<FJsonValueString>(Text);
            }
            Unavailable(Path, TEXT("Unsupported structured property type; exported text supplied"));
            TSharedPtr<FJsonObject> Fallback = MakeShared<FJsonObject>();
            Fallback->SetStringField(TEXT("property_type"), Property->GetCPPType());
            Fallback->SetStringField(TEXT("exported_text"), Text.Left(8192));
            Fallback->SetBoolField(TEXT("truncated"), Text.Len() > 8192);
            return MakeShared<FJsonValueObject>(Fallback);
        }
    };

    TSharedPtr<FJsonObject> ReadCascade(UParticleSystem* System, int32 EmitterIndex, int32 LODIndex)
    {
        if (EmitterIndex < -1 || LODIndex < -1 || (EmitterIndex >= 0 && !System->Emitters.IsValidIndex(EmitterIndex)))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Emitter/LOD selection out of range; use -1 for all"));
        }
        for (int32 Index = 0; Index < System->Emitters.Num(); ++Index)
        {
            UParticleEmitter* Emitter = System->Emitters[Index];
            if ((EmitterIndex == -1 || Index == EmitterIndex) && LODIndex >= 0
                && (!Emitter || !Emitter->LODLevels.IsValidIndex(LODIndex)))
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
                    TEXT("lod_index %d is unavailable on emitter_index %d; select one emitter or use -1"), LODIndex, Index));
            }
        }
        FCascadeReadContext Context(System);
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("name"), System->GetName());
        Result->SetStringField(TEXT("asset_path"), System->GetPathName());
        Result->SetStringField(TEXT("class"), System->GetClass()->GetPathName());
        Result->SetNumberField(TEXT("schema_version"), 1);
        Result->SetNumberField(TEXT("emitter_count"), System->Emitters.Num());
        Result->SetObjectField(TEXT("properties"), Context.Properties(System->GetClass(), System, System->GetPathName(), 0,
            {TEXT("Emitters"), TEXT("CurveEdSetup"), TEXT("ThumbnailImage")}));
        Result->SetArrayField(TEXT("excluded_editor_properties"), {
            MakeShared<FJsonValueString>(TEXT("CurveEdSetup")),
            MakeShared<FJsonValueString>(TEXT("ThumbnailImage"))});
        TArray<TSharedPtr<FJsonValue>> Emitters;
        for (int32 Index = 0; Index < System->Emitters.Num(); ++Index)
        {
            if (EmitterIndex != -1 && Index != EmitterIndex) continue;
            UParticleEmitter* Emitter = System->Emitters[Index];
            if (!Emitter)
            {
                Emitters.Add(Context.Unavailable(FString::Printf(TEXT("emitters[%d]"), Index), TEXT("Null emitter")));
                continue;
            }
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetNumberField(TEXT("index"), Index);
            Entry->SetStringField(TEXT("name"), Emitter->EmitterName.ToString());
            Entry->SetStringField(TEXT("object_path"), Emitter->GetPathName());
            Entry->SetStringField(TEXT("class"), Emitter->GetClass()->GetPathName());
            Entry->SetObjectField(TEXT("properties"), Context.Properties(Emitter->GetClass(), Emitter, Emitter->GetPathName(), 0,
                {TEXT("LODLevels")}));
            Entry->SetNumberField(TEXT("lod_count"), Emitter->LODLevels.Num());
            TArray<TSharedPtr<FJsonValue>> LODs;
            for (int32 LevelIndex = 0; LevelIndex < Emitter->LODLevels.Num(); ++LevelIndex)
            {
                if (LODIndex != -1 && LevelIndex != LODIndex) continue;
                UParticleLODLevel* Level = Emitter->LODLevels[LevelIndex];
                if (!Level)
                {
                    LODs.Add(Context.Unavailable(Emitter->GetPathName(), TEXT("Null LOD at index ") + FString::FromInt(LevelIndex)));
                    continue;
                }
                TSharedPtr<FJsonObject> LOD = MakeShared<FJsonObject>();
                LOD->SetNumberField(TEXT("index"), LevelIndex);
                LOD->SetNumberField(TEXT("level"), Level->Level);
                LOD->SetBoolField(TEXT("enabled"), Level->bEnabled);
                LOD->SetStringField(TEXT("object_path"), Level->GetPathName());
                LOD->SetField(TEXT("required_module"), Context.ObjectValue(Level->RequiredModule, 0));
                LOD->SetField(TEXT("spawn_module"), Context.ObjectValue(Level->SpawnModule, 0));
                LOD->SetField(TEXT("type_data_module"), Context.ObjectValue(Level->TypeDataModule, 0));
                LOD->SetField(TEXT("event_generator"), Context.ObjectValue(Level->EventGenerator, 0));
                TArray<TSharedPtr<FJsonValue>> Modules;
                for (UParticleModule* Module : Level->Modules)
                {
                    Modules.Add(Context.ObjectValue(Module, 0));
                }
                LOD->SetArrayField(TEXT("modules"), Modules);
                LOD->SetObjectField(TEXT("properties"), Context.Properties(Level->GetClass(), Level, Level->GetPathName(), 0,
                    {TEXT("RequiredModule"), TEXT("SpawnModule"), TEXT("TypeDataModule"), TEXT("EventGenerator"), TEXT("Modules")}));
                LODs.Add(MakeShared<FJsonValueObject>(LOD));
            }
            Entry->SetArrayField(TEXT("lods"), LODs);
            Emitters.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetNumberField(TEXT("selected_emitter_count"), Emitters.Num());
        Result->SetArrayField(TEXT("emitters"), Emitters);
        Result->SetObjectField(TEXT("objects"), Context.Objects);
        Result->SetObjectField(TEXT("referenced_assets"), Context.Assets);
        Result->SetNumberField(TEXT("object_count"), Context.Objects->Values.Num());
        Result->SetNumberField(TEXT("referenced_asset_count"), Context.Assets->Values.Num());
        Result->SetBoolField(TEXT("complete"), Context.Warnings.IsEmpty());
        Result->SetArrayField(TEXT("warnings"), Context.Warnings);
        Result->SetStringField(TEXT("scope"), TEXT("Authored reflected data for the selected emitters/LODs; no simulation, runtime overrides, external asset contents or custom binary payloads"));
        return Result;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleReadCascadeSystem(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || !AssetPath.StartsWith(TEXT("/"))
        || AssetPath.EndsWith(TEXT("/")) || AssetPath.Contains(TEXT(":")) || AssetPath.Contains(TEXT("\\")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("asset_path must be a full Unreal asset path, not a folder or subobject"));
    }
    int32 Indices[2] = {-1, -1};
    const TCHAR* Fields[2] = {TEXT("emitter_index"), TEXT("lod_index")};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (!Params->HasField(Fields[Index])) continue;
        double Value;
        if (!Params->TryGetNumberField(Fields[Index], Value) || !FMath::IsFinite(Value)
            || Value < -1 || Value > MAX_int32 || Value != FMath::FloorToDouble(Value))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString(Fields[Index]) + TEXT(" must be -1 or a nonnegative integer"));
        }
        Indices[Index] = static_cast<int32>(Value);
    }
    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    UParticleSystem* System = Cast<UParticleSystem>(Asset);
    if (!System)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Asset
            ? TEXT("Asset is not a Cascade ParticleSystem: ") + Asset->GetClass()->GetPathName()
            : TEXT("Failed to load asset: ") + AssetPath);
    }
    const bool WasDirty = System->GetOutermost()->IsDirty();
    TSharedPtr<FJsonObject> Result = ReadCascade(System, Indices[0], Indices[1]);
    Result->SetBoolField(TEXT("package_dirty_before_read"), WasDirty);
    Result->SetBoolField(TEXT("package_dirty_after_read"), System->GetOutermost()->IsDirty());
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Parameter/ParticleModuleParameterDynamic.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCascadeReadTest,
    "UnrealMCP.Cascade.ReadAuthoredData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPCascadeReadTest::RunTest(const FString& Parameters)
{
    UParticleSystem* System = NewObject<UParticleSystem>();
    UParticleSpriteEmitter* Emitter = NewObject<UParticleSpriteEmitter>(System);
    Emitter->EmitterName = TEXT("HoloStart");
    System->Emitters.Add(Emitter);
    UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(System);
    Lifetime->bEnabled = false;
    UDistributionFloatConstantCurve* Curve = NewObject<UDistributionFloatConstantCurve>(Lifetime);
    Curve->ConstantCurve.AddPoint(0.0f, 2.0f);
    Curve->ConstantCurve.AddPoint(1.0f, 4.0f);
    Curve->ConstantCurve.Points[0].InterpMode = CIM_CurveUser;
    Curve->ConstantCurve.Points[0].ArriveTangent = 0.25f;
    Curve->ConstantCurve.Points[0].LeaveTangent = 0.75f;
    Lifetime->Lifetime.Distribution = Curve;
    UParticleModuleParameterDynamic* Dynamic = NewObject<UParticleModuleParameterDynamic>(System);
    Dynamic->DynamicParams.SetNum(1);
    Dynamic->DynamicParams[0].ParamName = TEXT("Dissolve");
    Dynamic->DynamicParams[0].bUseEmitterTime = true;
    Dynamic->DynamicParams[0].bSpawnTimeOnly = false;
    UDistributionFloatUniform* Range = NewObject<UDistributionFloatUniform>(Dynamic);
    Range->Min = 0.2f;
    Range->Max = 0.8f;
    Dynamic->DynamicParams[0].ParamValue.Distribution = Range;
    UParticleModuleEventGenerator* Event = NewObject<UParticleModuleEventGenerator>(System);
    FParticleEvent_GenerateInfo EventInfo;
    EventInfo.Type = EPET_Death;
    EventInfo.CustomName = TEXT("StartLoop");
    EventInfo.Frequency = 1;
    Event->Events.Add(EventInfo);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        UParticleLODLevel* Level = NewObject<UParticleLODLevel>(Emitter);
        Level->Level = Index;
        Level->bEnabled = Index == 0;
        Level->RequiredModule = NewObject<UParticleModuleRequired>(System);
        Level->SpawnModule = NewObject<UParticleModuleSpawn>(System);
        Level->Modules = {Lifetime, Dynamic, Event};
        Level->EventGenerator = Event;
        Emitter->LODLevels.Add(Level);
    }
    const bool WasDirty = System->GetOutermost()->IsDirty();
    const TSharedPtr<FJsonObject> Result = ReadCascade(System, -1, -1);
    TestTrue(TEXT("Read succeeds"), Result->GetBoolField(TEXT("success")));
    TestTrue(TEXT("No unhandled reflected fields"), Result->GetBoolField(TEXT("complete")));
    const TSharedPtr<FJsonObject> Objects = Result->GetObjectField(TEXT("objects"));
    auto PropertiesOf = [&Objects](UObject* Object)
    {
        return Objects->GetObjectField(Object->GetPathName())->GetObjectField(TEXT("properties"));
    };
    const TArray<TSharedPtr<FJsonValue>>& LODs = Result->GetArrayField(TEXT("emitters"))[0]->AsObject()->GetArrayField(TEXT("lods"));
    TestEqual(TEXT("Both LODs retained"), LODs.Num(), 2);
    TestFalse(TEXT("Disabled LOD retained"), LODs[1]->AsObject()->GetBoolField(TEXT("enabled")));
    const TArray<TSharedPtr<FJsonValue>>& Modules = LODs[0]->AsObject()->GetArrayField(TEXT("modules"));
    TestEqual(TEXT("Module order preserved"), Modules[0]->AsObject()->GetStringField(TEXT("object_ref")), Lifetime->GetPathName());
    TestEqual(TEXT("Shared module identity"), LODs[1]->AsObject()->GetArrayField(TEXT("modules"))[0]->AsObject()->GetStringField(TEXT("object_ref")), Lifetime->GetPathName());
    TestFalse(TEXT("Bitfield boolean"), PropertiesOf(Lifetime)->GetBoolField(TEXT("bEnabled")));
    const TSharedPtr<FJsonObject> FirstKey = PropertiesOf(Curve)->GetObjectField(TEXT("ConstantCurve"))->GetArrayField(TEXT("Points"))[0]->AsObject();
    TestEqual(TEXT("Curve value"), FirstKey->GetNumberField(TEXT("OutVal")), 2.0);
    TestEqual(TEXT("Curve interpolation"), FirstKey->GetStringField(TEXT("InterpMode")), FString(TEXT("CIM_CurveUser")));
    TestEqual(TEXT("Arrive tangent"), FirstKey->GetNumberField(TEXT("ArriveTangent")), 0.25);
    TestEqual(TEXT("Leave tangent"), FirstKey->GetNumberField(TEXT("LeaveTangent")), 0.75);
    const TSharedPtr<FJsonObject> Parameter = PropertiesOf(Dynamic)->GetArrayField(TEXT("DynamicParams"))[0]->AsObject();
    TestEqual(TEXT("Dynamic channel"), Parameter->GetStringField(TEXT("ParamName")), FString(TEXT("Dissolve")));
    TestTrue(TEXT("Emitter time flag"), Parameter->GetBoolField(TEXT("bUseEmitterTime")));
    TestFalse(TEXT("Update every frame flag"), Parameter->GetBoolField(TEXT("bSpawnTimeOnly")));
    TestEqual(TEXT("Distribution minimum"), static_cast<float>(PropertiesOf(Range)->GetNumberField(TEXT("Min"))), 0.2f);
    TestEqual(TEXT("Distribution maximum"), static_cast<float>(PropertiesOf(Range)->GetNumberField(TEXT("Max"))), 0.8f);
    const TSharedPtr<FJsonObject> EventFields = PropertiesOf(Event)->GetArrayField(TEXT("Events"))[0]->AsObject();
    TestEqual(TEXT("Event type"), EventFields->GetStringField(TEXT("Type")), FString(TEXT("EPET_Death")));
    TestEqual(TEXT("Event name"), EventFields->GetStringField(TEXT("CustomName")), FString(TEXT("StartLoop")));
    const TSharedPtr<FJsonObject> Selected = ReadCascade(System, 0, 1);
    TestEqual(TEXT("LOD filter"), Selected->GetArrayField(TEXT("emitters"))[0]->AsObject()->GetArrayField(TEXT("lods")).Num(), 1);
    TestFalse(TEXT("Invalid emitter fails"), ReadCascade(System, 1, -1)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Invalid LOD fails"), ReadCascade(System, 0, 2)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Dirty state preserved"), System->GetOutermost()->IsDirty(), WasDirty);
    Curve->ConstantCurve.Points.SetNum(4100);
    const TSharedPtr<FJsonObject> Limited = ReadCascade(System, 0, 0);
    TestFalse(TEXT("Truncation cannot claim completeness"), Limited->GetBoolField(TEXT("complete")));
    TestTrue(TEXT("Truncation warning present"), Limited->GetArrayField(TEXT("warnings")).Num() > 0);
    return !HasAnyErrors();
}
#endif