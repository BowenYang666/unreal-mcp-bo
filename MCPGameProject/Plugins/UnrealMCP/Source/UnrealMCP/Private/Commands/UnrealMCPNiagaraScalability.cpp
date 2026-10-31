#include "Commands/UnrealMCPNiagaraScalability.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraSettings.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEffectType.h"
#include "NiagaraRendererProperties.h"
#include "Commands/UnrealMCPNiagaraCommands.h"
#include "EditorAssetLibrary.h"
#include "Curves/CurveFloat.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"

namespace UnrealMCPNiagaraScalability
{
    TSharedRef<FJsonObject> Context()
    {
        auto Result = MakeShared<FJsonObject>();
        const auto* Profile = UDeviceProfileManager::Get().GetActiveProfile();
        if (Profile) Result->SetStringField(TEXT("active_device_profile"), Profile->GetPathName());
        else Result->SetField(TEXT("active_device_profile"), MakeShared<FJsonValueNull>());
        auto Variables = MakeShared<FJsonObject>();
        for (const TCHAR* Name : {TEXT("sg.EffectsQuality"), TEXT("fx.Niagara.QualityLevel")})
        {
            auto Entry = MakeShared<FJsonObject>();
            const auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
            if (Variable)
            {
                Entry->SetStringField(TEXT("value"), Variable->GetString());
                Entry->SetNumberField(TEXT("flags"), static_cast<uint32>(Variable->GetFlags()));
                Entry->SetStringField(TEXT("state"), TEXT("read"));
            }
            else Entry->SetStringField(TEXT("state"), TEXT("unregistered"));
            Variables->SetObjectField(Name, Entry);
        }
        Result->SetObjectField(TEXT("quality_cvars"), Variables);
        Result->SetStringField(TEXT("semantics"), TEXT("Current context sampled read-only; not an explicit-profile resolver or an Editor preview quality override value"));
        return Result;
    }

    void FReader::Missing(const FString& Path, const FString& Reason)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Path);
        Item->SetStringField(TEXT("reason"), Reason);
        Unsupported.Add(MakeShared<FJsonValueObject>(Item));
        FieldStates->SetStringField(Path, TEXT("unsupported"));
    }

    TSharedPtr<FJsonValue> FReader::Value(const FProperty* Property, const void* Data, const FString& Path, int32 Depth)
    {
        if (++Values > 200000 || Depth > 24)
        {
            Missing(Path, TEXT("Serialization bounds exceeded"));
            return MakeShared<FJsonValueNull>();
        }
        FieldStates->SetStringField(Path, TEXT("read"));
        if (const auto* Bool = CastField<FBoolProperty>(Property)) return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Data));
        if (const auto* Enum = CastField<FEnumProperty>(Property))
            return MakeShared<FJsonValueString>(Enum->GetEnum()->GetNameStringByValue(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Data)));
        if (const auto* Byte = CastField<FByteProperty>(Property))
        {
            if (Byte->Enum) return MakeShared<FJsonValueString>(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(Data)));
        }
        if (const auto* Number = CastField<FNumericProperty>(Property))
        {
            const bool Unsigned = CastField<FUInt16Property>(Property) || CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property) || CastField<FByteProperty>(Property);
            const double NumberValue = Number->IsFloatingPoint() ? Number->GetFloatingPointPropertyValue(Data)
                : Unsigned ? static_cast<double>(Number->GetUnsignedIntPropertyValue(Data)) : static_cast<double>(Number->GetSignedIntPropertyValue(Data));
            if (!FMath::IsFinite(NumberValue)) { Missing(Path, TEXT("Nonfinite numeric value")); return MakeShared<FJsonValueNull>(); }
            if (Number->IsInteger() && FMath::Abs(NumberValue) > 9007199254740991.0) { Missing(Path, TEXT("Integer exceeds lossless JSON range")); return MakeShared<FJsonValueNull>(); }
            return MakeShared<FJsonValueNumber>(NumberValue);
        }
        if (const auto* String = CastField<FStrProperty>(Property)) return MakeShared<FJsonValueString>(String->GetPropertyValue(Data));
        if (const auto* Name = CastField<FNameProperty>(Property)) return MakeShared<FJsonValueString>(Name->GetPropertyValue(Data).ToString());
        if (const auto* Text = CastField<FTextProperty>(Property)) return MakeShared<FJsonValueString>(Text->GetPropertyValue(Data).ToString());
        if (const auto* Soft = CastField<FSoftObjectProperty>(Property))
        {
            const auto Reference = Soft->GetPropertyValue(Data);
            if (Reference.IsNull()) { FieldStates->SetStringField(Path, TEXT("unbound")); return MakeShared<FJsonValueNull>(); }
            return MakeShared<FJsonValueString>(Reference.ToString());
        }
        if (const auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            const UObject* Object = ObjectProperty->GetObjectPropertyValue(Data);
            if (!Object) { FieldStates->SetStringField(Path, TEXT("unbound")); return MakeShared<FJsonValueNull>(); }
            auto Reference = MakeShared<FJsonObject>();
            Reference->SetStringField(TEXT("path"), Object->GetPathName());
            Reference->SetStringField(TEXT("class_path"), Object->GetClass()->GetPathName());
            if (const auto* Curve = Cast<UCurveFloat>(Object))
                Reference->SetObjectField(TEXT("curve"), Structure(FRichCurve::StaticStruct(), &Curve->FloatCurve, Path + TEXT(".curve"), Depth + 1));
            return MakeShared<FJsonValueObject>(Reference);
        }
        if (const auto* Struct = CastField<FStructProperty>(Property)) return MakeShared<FJsonValueObject>(Structure(Struct->Struct, Data, Path, Depth + 1));
        if (const auto* Array = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Helper(Array, Data);
            TArray<TSharedPtr<FJsonValue>> Items;
            if (Helper.Num() > 16384) { Missing(Path, TEXT("Array exceeds 16384 elements")); return MakeShared<FJsonValueNull>(); }
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
                Items.Add(Value(Array->Inner, Helper.GetRawPtr(Index), Path + FString::Printf(TEXT("[%d]"), Index), Depth + 1));
            return MakeShared<FJsonValueArray>(Items);
        }
        Missing(Path, TEXT("Unsupported reflected type: ") + Property->GetCPPType());
        return MakeShared<FJsonValueNull>();
    }

    TSharedRef<FJsonObject> FReader::Structure(const UStruct* Type, const void* Data, const FString& Path, int32 Depth)
    {
        auto Result = MakeShared<FJsonObject>();
        if (!Type || !Data) { Missing(Path, TEXT("Missing struct data")); return Result; }
        FStructOnScope Defaults(Type);
        for (TFieldIterator<FProperty> Field(Type); Field; ++Field)
        {
            if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
            if (Field->ArrayDim != 1) { Missing(Path + TEXT(".") + Field->GetName(), TEXT("Static array unsupported")); continue; }
            Result->SetField(Field->GetName(), Value(*Field, Field->ContainerPtrToValuePtr<void>(Data), Path + TEXT(".") + Field->GetName(), Depth));
            if (Defaults.GetStructMemory() && !CastField<FStructProperty>(*Field) && !CastField<FArrayProperty>(*Field)) DefaultMatches->SetBoolField(Path + TEXT(".") + Field->GetName(),
                Field->Identical(Field->ContainerPtrToValuePtr<void>(Data), Field->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory())));
        }
        if (Type == FNiagaraPlatformSet::StaticStruct())
        {
            const auto* Platform = static_cast<const FNiagaraPlatformSet*>(Data);
            const auto& Names = GetDefault<UNiagaraSettings>()->QualityLevels;
            TArray<TSharedPtr<FJsonValue>> Qualities;
            for (int32 Index = 0; Index < Names.Num(); ++Index)
            {
                auto Quality = MakeShared<FJsonObject>();
                Quality->SetNumberField(TEXT("index"), Index);
                Quality->SetStringField(TEXT("name"), Names[Index].ToString());
                if (Index < 32) Quality->SetBoolField(TEXT("mask_enabled"), (static_cast<uint32>(Platform->QualityLevelMask) & (uint32(1) << Index)) != 0);
                else { Quality->SetField(TEXT("mask_enabled"), MakeShared<FJsonValueNull>()); Missing(Path, TEXT("Quality index exceeds 32-bit mask")); }
                Qualities.Add(MakeShared<FJsonValueObject>(Quality));
            }
            Result->SetArrayField(TEXT("quality_levels"), Qualities);
            Result->SetBoolField(TEXT("editor_preview_override_bound"), Platform->OnOverrideQualityLevelDelegate.IsBound());
            Result->SetStringField(TEXT("semantics"), TEXT("Saved conditions only; mask expansion is not resolved eligibility. DeviceProfile Default inherits from parent/default rules; CVarConditions remain unevaluated."));
        }
        else if (Type == FNiagaraDeviceProfileStateEntry::StaticStruct())
        {
            const auto* Entry = static_cast<const FNiagaraDeviceProfileStateEntry*>(Data);
            const auto& Names = GetDefault<UNiagaraSettings>()->QualityLevels;
            TArray<TSharedPtr<FJsonValue>> States;
            for (int32 Index = 0; Index < FMath::Min(Names.Num(), 32); ++Index)
            {
                auto State = MakeShared<FJsonObject>();
                State->SetNumberField(TEXT("index"), Index); State->SetStringField(TEXT("name"), Names[Index].ToString());
                const auto Selection = Entry->GetState(Index);
                State->SetStringField(TEXT("state"), Selection == ENiagaraPlatformSelectionState::Default ? TEXT("Default")
                    : Selection == ENiagaraPlatformSelectionState::Enabled ? TEXT("Enabled") : TEXT("Disabled"));
                States.Add(MakeShared<FJsonValueObject>(State));
            }
            Result->SetArrayField(TEXT("quality_states"), States);
        }
        return Result;
    }

    TSharedRef<FJsonObject> FReader::Properties(const UObject* Object, const TArray<FName>& Names, const FString& Path)
    {
        auto Result = MakeShared<FJsonObject>();
        for (FName Name : Names)
        {
            const FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(Name) : nullptr;
            const FString FieldPath = Path + TEXT(".") + Name.ToString();
            if (!Property) { Result->SetField(Name.ToString(), MakeShared<FJsonValueNull>()); Missing(FieldPath, TEXT("Field unavailable in this class/engine")); continue; }
            const void* Data = Property->ContainerPtrToValuePtr<void>(Object);
            Result->SetField(Name.ToString(), Value(Property, Data, FieldPath, 0));
            const UObject* Defaults = Object->GetClass()->GetDefaultObject();
            if (Defaults && !CastField<FStructProperty>(Property) && !CastField<FArrayProperty>(Property))
                DefaultMatches->SetBoolField(FieldPath, Property->Identical(Data, Property->ContainerPtrToValuePtr<void>(Defaults)));
        }
        return Result;
    }

    TSharedRef<FJsonObject> FReader::Finish(const TSharedRef<FJsonObject>& Result)
    {
        Result->SetNumberField(TEXT("contract_version"), 1);
        Result->SetBoolField(TEXT("complete"), Unsupported.IsEmpty());
        Result->SetArrayField(TEXT("unsupported"), Unsupported);
        Result->SetObjectField(TEXT("field_states"), FieldStates);
        Result->SetObjectField(TEXT("matches_engine_default"), DefaultMatches);
        Result->SetStringField(TEXT("default_semantics"), TEXT("Equality to current engine struct/class defaults, not proof of explicit serialization or an enabled override. All values remain saved configuration, not resolved values."));
        Result->SetStringField(TEXT("scope"), TEXT("saved_configuration_not_resolved_or_runtime_state"));
        return Result;
    }

    TSharedRef<FJsonObject> EffectType(const UNiagaraEffectType* Asset)
    {
        FReader Reader;
        auto Result = Reader.Properties(Asset, {TEXT("UpdateFrequency"), TEXT("CullReaction"), TEXT("bAllowCullingForLocalPlayers"),
            TEXT("SignificanceHandler"), TEXT("SystemScalabilitySettings"), TEXT("EmitterScalabilitySettings")}, TEXT("effect_type"));
        Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
        Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
        Result->SetObjectField(TEXT("read_context"), Context());
        const UObject* Significance = Asset->GetSignificanceHandler();
        if (Significance)
        {
            TArray<FName> Names;
            for (TFieldIterator<FProperty> Field(Significance->GetClass()); Field; ++Field)
                if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) Names.Add(Field->GetFName());
            auto Details = Reader.Properties(Significance, Names, TEXT("effect_type.SignificanceHandler.settings"));
            Result->GetObjectField(TEXT("SignificanceHandler"))->SetObjectField(TEXT("settings"), Details);
        }
        return Reader.Finish(Result);
    }

    TSharedRef<FJsonObject> System(const UNiagaraSystem* Asset)
    {
        FReader Reader;
        auto Result = Reader.Properties(Asset, {TEXT("EffectType"), TEXT("Platforms"), TEXT("bOverrideScalabilitySettings"),
            TEXT("SystemScalabilityOverrides"), TEXT("bOverrideAllowCullingForLocalPlayers"), TEXT("bAllowCullingForLocalPlayersOverride")}, TEXT("system"));
        Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
        Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
        if (const auto* Effect = Asset->GetEffectType())
        {
            auto Details = EffectType(Effect);
            Result->SetObjectField(TEXT("effect_type_details"), Details);
            if (!Details->GetBoolField(TEXT("complete"))) Reader.Missing(TEXT("system.effect_type_details"), TEXT("Referenced Effect Type has unsupported fields; inspect its own unsupported list"));
        }
        else Result->SetField(TEXT("effect_type_details"), MakeShared<FJsonValueNull>());
        Result->SetStringField(TEXT("inheritance_semantics"), TEXT("Effect Type saved rules and System override rows are separate. A stored value does not activate its override; no final context-dependent values are resolved."));
        return Reader.Finish(Result);
    }

    TSharedRef<FJsonObject> Emitter(const FNiagaraEmitterHandle& Handle)
    {
        FReader Reader;
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("handle_id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
        Result->SetStringField(TEXT("display_name"), Handle.GetName().ToString());
        Result->SetStringField(TEXT("unique_name"), Handle.GetUniqueInstanceName());
        Result->SetBoolField(TEXT("authoring_enabled"), Handle.GetIsEnabled());
        const bool Stateless = Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless;
        Result->SetStringField(TEXT("mode"), Stateless ? TEXT("Stateless") : TEXT("Stateful"));
        if (const auto* Base = Handle.GetEmitterBase()) Result->SetStringField(TEXT("emitter_object_path"), Base->GetPathName());
        else Result->SetField(TEXT("emitter_object_path"), MakeShared<FJsonValueNull>());
        if (Stateless)
        {
            Reader.Missing(TEXT("emitter"), TEXT("Stateless saved scalability is not implemented; no defaults or empty rules substituted"));
            Result->SetField(TEXT("Platforms"), MakeShared<FJsonValueNull>());
            Result->SetField(TEXT("ScalabilityOverrides"), MakeShared<FJsonValueNull>());
            Result->SetField(TEXT("selected_version_guid"), MakeShared<FJsonValueNull>());
            Result->SetStringField(TEXT("version_status"), TEXT("not_applicable_stateless"));
            return Reader.Finish(Result);
        }
        const auto Versioned = Handle.GetInstance();
        Result->SetBoolField(TEXT("versioning_enabled"), Versioned.Emitter && Versioned.Emitter->IsVersioningEnabled());
        Result->SetStringField(TEXT("selected_version_guid"), Versioned.Version.ToString(EGuidFormats::DigitsWithHyphens));
        Result->SetBoolField(TEXT("uses_exposed_default"), !Versioned.Version.IsValid());
        const auto* Data = Handle.GetEmitterData();
        if (!Data)
        {
            Reader.Missing(TEXT("emitter.selected_version"), TEXT("Handle selected data unavailable; latest version is not substituted"));
            Result->SetStringField(TEXT("version_status"), TEXT("unresolved"));
        }
        else
        {
            Result->SetStringField(TEXT("version_status"), Versioned.Version.IsValid() ? TEXT("selected_guid") : TEXT("engine_exposed_default"));
            Result->SetStringField(TEXT("resolved_version_guid"), Data->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
            Result->SetObjectField(TEXT("Platforms"), Reader.Structure(FNiagaraPlatformSet::StaticStruct(), &Data->Platforms, TEXT("emitter.Platforms")));
            Result->SetObjectField(TEXT("ScalabilityOverrides"), Reader.Structure(FNiagaraEmitterScalabilityOverrides::StaticStruct(), &Data->ScalabilityOverrides, TEXT("emitter.ScalabilityOverrides")));
        }
        Result->SetStringField(TEXT("enabled_semantics"), TEXT("Authoring enabled only, not resolved quality eligibility, runtime activation, particle generation or culling state"));
        return Reader.Finish(Result);
    }

    TSharedRef<FJsonObject> Renderer(const UNiagaraRendererProperties* Asset)
    {
        FReader Reader;
        auto Result = Reader.Properties(Asset, {TEXT("Platforms")}, TEXT("renderer"));
        Result->SetStringField(TEXT("object_path"), Asset->GetPathName());
        Result->SetBoolField(TEXT("authoring_enabled"), Asset->GetIsEnabled());
        Result->SetStringField(TEXT("semantics"), TEXT("Renderer-only saved conditions; does not replace System or Emitter conditions and is not a runtime visibility result"));
        return Reader.Finish(Result);
    }

    TSharedPtr<FJsonObject> ReadEffectTypeAsset(const TSharedPtr<FJsonObject>& Params)
    {
        FString Path;
        if (!Params || !Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/")))
        {
            auto Error = MakeShared<FJsonObject>(); Error->SetBoolField(TEXT("success"), false);
            Error->SetStringField(TEXT("error"), TEXT("Require an absolute Effect Type asset_path")); return Error;
        }
        auto* Asset = Cast<UNiagaraEffectType>(UEditorAssetLibrary::LoadAsset(Path));
        if (!Asset)
        {
            auto Error = MakeShared<FJsonObject>(); Error->SetBoolField(TEXT("success"), false);
            Error->SetStringField(TEXT("error"), TEXT("Asset is not a Niagara Effect Type")); return Error;
        }
        auto Result = EffectType(Asset);
        Result->SetBoolField(TEXT("success"), true);
        Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        return Result;
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraPlatformReadTest, "UnrealMCP.Niagara.Scalability.Platforms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraPlatformReadTest::RunTest(const FString& Parameters)
{
    for (int32 Mask : {-1, -29, -4})
    {
        FNiagaraPlatformSet Platform(Mask);
        FNiagaraPlatformSetCVarCondition Condition;
        Condition.CVarName = TEXT("MCP.ReadOnly.UnregisteredCondition");
        Condition.PassResponse = ENiagaraCVarConditionResponse::Enable;
        Condition.FailResponse = ENiagaraCVarConditionResponse::Disable;
        Condition.bUseMinInt = true; Condition.MinInt = 7;
        Platform.CVarConditions.Add(Condition);
        FNiagaraDeviceProfileStateEntry Entry;
        Entry.ProfileName = TEXT("MCP_Test_Profile");
        Entry.SetState(0, ENiagaraPlatformSelectionState::Enabled);
        Entry.SetState(1, ENiagaraPlatformSelectionState::Disabled);
        Platform.DeviceProfileStates.Add(Entry);
        UnrealMCPNiagaraScalability::FReader Reader;
        auto Result = Reader.Finish(Reader.Structure(FNiagaraPlatformSet::StaticStruct(), &Platform));
        TestTrue(TEXT("Full platform struct readable"), Result->GetBoolField(TEXT("complete")));
        TestEqual(TEXT("Signed raw mask preserved"), Result->GetIntegerField(TEXT("QualityLevelMask")), Mask);
        const auto Qualities = Result->GetArrayField(TEXT("quality_levels"));
        TestEqual(TEXT("Dynamic engine quality count"), Qualities.Num(), GetDefault<UNiagaraSettings>()->QualityLevels.Num());
        for (int32 Index = 0; Index < FMath::Min(Qualities.Num(), 32); ++Index)
            TestEqual(TEXT("Signed mask bits"), Qualities[Index]->AsObject()->GetBoolField(TEXT("mask_enabled")), (static_cast<uint32>(Mask) & (uint32(1) << Index)) != 0);
        const auto SavedCondition = Result->GetArrayField(TEXT("CVarConditions"))[0]->AsObject();
        TestEqual(TEXT("CVar condition retained without registration/evaluation"), SavedCondition->GetStringField(TEXT("CVarName")), FString(TEXT("MCP.ReadOnly.UnregisteredCondition")));
        TestEqual(TEXT("Pass response typed"), SavedCondition->GetStringField(TEXT("PassResponse")), FString(TEXT("Enable")));
        TestTrue(TEXT("Comparison flag retained"), SavedCondition->GetBoolField(TEXT("bUseMinInt")));
        TestEqual(TEXT("Comparison bound retained"), SavedCondition->GetIntegerField(TEXT("MinInt")), 7);
        const auto Device = Result->GetArrayField(TEXT("DeviceProfileStates"))[0]->AsObject();
        TestEqual(TEXT("Profile identity retained"), Device->GetStringField(TEXT("ProfileName")), FString(TEXT("MCP_Test_Profile")));
        const auto States = Device->GetArrayField(TEXT("quality_states"));
        if (States.Num() >= 3)
        {
            TestEqual(TEXT("Explicit Enabled retained"), States[0]->AsObject()->GetStringField(TEXT("state")), FString(TEXT("Enabled")));
            TestEqual(TEXT("Explicit Disabled retained"), States[1]->AsObject()->GetStringField(TEXT("state")), FString(TEXT("Disabled")));
            TestEqual(TEXT("Default inheritance is not false"), States[2]->AsObject()->GetStringField(TEXT("state")), FString(TEXT("Default")));
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraSavedRulesTest, "UnrealMCP.Niagara.Scalability.SavedRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraSavedRulesTest::RunTest(const FString& Parameters)
{
    FNiagaraEmitterScalabilityOverrides Overrides;
    auto& Low = Overrides.Overrides.AddDefaulted_GetRef();
    Low.Platforms = FNiagaraPlatformSet(-29);
    Low.bOverrideSpawnCountScale = true; Low.bScaleSpawnCount = true; Low.SpawnCountScale = 0.1f;
    auto& High = Overrides.Overrides.AddDefaulted_GetRef();
    High.Platforms = FNiagaraPlatformSet(-4);
    High.bOverrideSpawnCountScale = false; High.bScaleSpawnCount = false; High.SpawnCountScale = 1.0f;
    UnrealMCPNiagaraScalability::FReader Reader;
    const auto Result = Reader.Finish(Reader.Structure(FNiagaraEmitterScalabilityOverrides::StaticStruct(), &Overrides));
    TestTrue(TEXT("All override fields supported"), Result->GetBoolField(TEXT("complete")));
    const auto Rows = Result->GetArrayField(TEXT("Overrides"));
    TestEqual(TEXT("Row order and count"), Rows.Num(), 2);
    TestTrue(TEXT("Low override active flag stored"), Rows[0]->AsObject()->GetBoolField(TEXT("bOverrideSpawnCountScale")));
    TestTrue(TEXT("Low scale flag stored"), Rows[0]->AsObject()->GetBoolField(TEXT("bScaleSpawnCount")));
    TestTrue(TEXT("Low multiplier stored"), FMath::IsNearlyEqual(Rows[0]->AsObject()->GetNumberField(TEXT("SpawnCountScale")), 0.1, 0.000001));
    TestFalse(TEXT("High row is not a forced override"), Rows[1]->AsObject()->GetBoolField(TEXT("bOverrideSpawnCountScale")));
    TestFalse(TEXT("High disabled scale flag retained"), Rows[1]->AsObject()->GetBoolField(TEXT("bScaleSpawnCount")));
    TestEqual(TEXT("High saved default is still returned"), Rows[1]->AsObject()->GetNumberField(TEXT("SpawnCountScale")), 1.0);
    FRuntimeFloatCurve Curve;
    Curve.GetRichCurve()->AddKey(0, 0.25f); Curve.GetRichCurve()->AddKey(1, 1);
    UnrealMCPNiagaraScalability::FReader CurveReader;
    const auto SavedCurve = CurveReader.Finish(CurveReader.Structure(FRuntimeFloatCurve::StaticStruct(), &Curve));
    TestTrue(TEXT("Runtime curve supported"), SavedCurve->GetBoolField(TEXT("complete")));
    TestEqual(TEXT("Curve keys not replaced with opaque text"), SavedCurve->GetObjectField(TEXT("EditorCurveData"))->GetArrayField(TEXT("Keys")).Num(), 2);
    TestTrue(TEXT("Unbound external curve explicit"), SavedCurve->GetField<EJson::Null>(TEXT("ExternalCurve")).IsValid());
    auto Effect = NewObject<UNiagaraEffectType>();
    auto& Settings = Effect->SystemScalabilitySettings.Settings.AddDefaulted_GetRef();
    Settings.MaxInstances = 4; Settings.MaxSystemInstances = 6;
    Settings.bCullMaxInstanceCount = false; Settings.bCullPerSystemMaxInstanceCount = true;
    Effect->GetPackage()->SetDirtyFlag(false);
    const auto EffectResult = UnrealMCPNiagaraScalability::EffectType(Effect);
    TestTrue(TEXT("Effect Type complete"), EffectResult->GetBoolField(TEXT("complete")));
    const auto SavedSettings = EffectResult->GetObjectField(TEXT("SystemScalabilitySettings"))->GetArrayField(TEXT("Settings"))[0]->AsObject();
    TestEqual(TEXT("Inactive stored instance count still visible"), SavedSettings->GetIntegerField(TEXT("MaxInstances")), 4);
    TestFalse(TEXT("Inactive instance flag not promoted to true"), SavedSettings->GetBoolField(TEXT("bCullMaxInstanceCount")));
    TestTrue(TEXT("Per-system count flag preserved"), SavedSettings->GetBoolField(TEXT("bCullPerSystemMaxInstanceCount")));
    TestEqual(TEXT("Per-system count preserved"), SavedSettings->GetIntegerField(TEXT("MaxSystemInstances")), 6);
    TestFalse(TEXT("Read leaves package clean"), Effect->GetPackage()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraReadCompatibilityTest, "UnrealMCP.Niagara.Scalability.ReadCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraReadCompatibilityTest::RunTest(const FString& Parameters)
{
    const FString Path = TEXT("/Niagara/DefaultAssets/DefaultSystem");
    auto* Asset = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Path));
    if (!TestNotNull(TEXT("Engine reference system"), Asset)) return false;
    const bool Dirty = Asset->GetPackage()->IsDirty();
    const auto Before = UnrealMCPNiagaraScalability::Context();
    FUnrealMCPNiagaraCommands Commands;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("path"), Path);
    const auto Result = Commands.HandleCommand(TEXT("read_niagara_system"), Params);
    if (!TestTrue(TEXT("Legacy and new read data returned"), Result->HasField(TEXT("emitters")))) return false;
    TestEqual(TEXT("Native reader capability version"), Result->GetIntegerField(TEXT("reader_contract")), 2);
    TestEqual(TEXT("Legacy user parameter count retained"), Result->GetIntegerField(TEXT("user_parameter_count")), Result->GetArrayField(TEXT("user_parameters")).Num());
    const auto Emitters = Result->GetArrayField(TEXT("emitters"));
    if (!TestEqual(TEXT("No Handle filtering"), Emitters.Num(), Asset->GetEmitterHandles().Num())) return false;
    for (int32 Index = 0; Index < Emitters.Num(); ++Index)
    {
        const auto Emitter = Emitters[Index]->AsObject();
        const auto& Handle = Asset->GetEmitterHandles()[Index];
        TestEqual(TEXT("Stable Handle ID"), Emitter->GetStringField(TEXT("handle_id")), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
        TestEqual(TEXT("Authoring enabled unchanged"), Emitter->GetBoolField(TEXT("enabled")), Handle.GetIsEnabled());
        const auto* Data = Handle.GetEmitterData();
        if (!Data) continue;
        TestEqual(TEXT("Actually selected version"), Emitter->GetObjectField(TEXT("scalability"))->GetStringField(TEXT("resolved_version_guid")), Data->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
        const auto Renderers = Emitter->GetArrayField(TEXT("renderers"));
        for (const auto& Value : Renderers)
        {
            const auto Renderer = Value->AsObject();
            const int32 Slot = Renderer->GetIntegerField(TEXT("renderer_index"));
            TestEqual(TEXT("Renderer identity uses source slot"), Renderer->GetStringField(TEXT("object_path")), Data->GetRenderers()[Slot]->GetPathName());
            TestTrue(TEXT("Legacy Renderer platform data retained"), Renderer->HasField(TEXT("platforms")));
            if (Renderer->GetStringField(TEXT("type")).Contains(TEXT("Sprite")))
                TestTrue(TEXT("Legacy material and binding fields retained"), Renderer->HasField(TEXT("material")) && Renderer->HasField(TEXT("material_user_binding")));
        }
        for (const TCHAR* Section : {TEXT("emitter_spawn_script"), TEXT("emitter_update_script"), TEXT("spawn_script"), TEXT("update_script")})
        {
            if (!Emitter->HasField(Section)) continue;
            const auto Script = Emitter->GetObjectField(Section);
            TestTrue(TEXT("Rapid iteration fields retained"), Script->HasField(TEXT("rapid_iteration_parameters")));
            for (const auto& ModuleValue : Script->GetArrayField(TEXT("modules")))
            {
                const auto Module = ModuleValue->AsObject();
                TestTrue(TEXT("Module identity and saved pins"), Module->HasField(TEXT("node_guid")) && Module->HasField(TEXT("selected_version_guid")) && Module->HasField(TEXT("input_pins")));
            }
        }
    }
    const auto After = UnrealMCPNiagaraScalability::Context();
    TestEqual(TEXT("Active Profile unchanged"), Before->GetStringField(TEXT("active_device_profile")), After->GetStringField(TEXT("active_device_profile")));
    for (const TCHAR* Name : {TEXT("sg.EffectsQuality"), TEXT("fx.Niagara.QualityLevel")})
    {
        const auto Old = Before->GetObjectField(TEXT("quality_cvars"))->GetObjectField(Name);
        const auto Now = After->GetObjectField(TEXT("quality_cvars"))->GetObjectField(Name);
        TestEqual(TEXT("Quality value unchanged"), Old->GetStringField(TEXT("value")), Now->GetStringField(TEXT("value")));
        TestEqual(TEXT("CVar source flags unchanged"), Old->GetNumberField(TEXT("flags")), Now->GetNumberField(TEXT("flags")));
    }
    TestEqual(TEXT("Read preserves package dirty state"), Asset->GetPackage()->IsDirty(), Dirty);
    return !HasAnyErrors();
}
#endif
