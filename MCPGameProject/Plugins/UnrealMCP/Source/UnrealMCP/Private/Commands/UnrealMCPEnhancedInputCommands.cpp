#include "Commands/UnrealMCPEnhancedInputCommands.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "PlayerMappableKeySettings.h"
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "UObject/Package.h"
#include "Internationalization/Text.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"

namespace UnrealMCPEnhancedInput
{
    namespace
    {
        TSharedPtr<FJsonObject> Failure(const FString& Error)
        {
            auto Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("success"), false);
            Result->SetBoolField(TEXT("modified"), false);
            Result->SetBoolField(TEXT("saved"), false);
            Result->SetStringField(TEXT("stage"), TEXT("preflight"));
            Result->SetStringField(TEXT("error"), Error);
            return Result;
        }

        UInputMappingContext* Load(const TSharedPtr<FJsonObject>& Params, FString& Error)
        {
            FString Path;
            if (!Params || !Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
                || Path.Len() >= NAME_SIZE || Path.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Path))
            { Error = TEXT("Require an exact /Game package asset_path without an object suffix"); return nullptr; }
            auto* Asset = Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(Path));
            if (Asset && Asset->GetPackage()->GetName() != Path) { Error = TEXT("Redirected/noncanonical IMC path refused; read the actual package path"); return nullptr; }
            if (!Asset) Error = TEXT("Asset is not an InputMappingContext: ") + Path;
            return Asset;
        }

        FString Json(const TSharedPtr<FJsonValue>& Value)
        {
            FString Text;
            FJsonSerializer::Serialize(Value, TEXT(""), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
            return Text;
        }

        struct FSnapshotReader
        {
            int32 Values = 0;
            int32 Rows = 0;
            int32 Failures = 0;
            int32 StringCharacters = 0;
            TArray<TSharedPtr<FJsonValue>> Unsupported;
            TSet<const UObject*> Expanded;

            TSharedPtr<FJsonValue> Missing(const FString& Path, const FString& Reason)
            {
                ++Failures;
                auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), Path); Entry->SetStringField(TEXT("reason"), Reason);
                if (Unsupported.Num() < 128) Unsupported.Add(MakeShared<FJsonValueObject>(Entry)); return MakeShared<FJsonValueNull>();
            }

            TSharedPtr<FJsonValue> String(const FString& Text, const FString& Path)
            {
                if (Text.Len() > 65536 || StringCharacters > 524288 - Text.Len()) return Missing(Path, TEXT("Snapshot string budget exceeded"));
                StringCharacters += Text.Len(); return MakeShared<FJsonValueString>(Text);
            }

            TSharedRef<FJsonObject> Object(const UObject* Asset, const FString& Path, int32 Depth = 0)
            {
                auto Result = MakeShared<FJsonObject>();
                Result->SetStringField(TEXT("path"), Asset->GetPathName()); Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
                if (Asset->GetOuter()) Result->SetStringField(TEXT("outer_path"), Asset->GetOuter()->GetPathName());
                if (Expanded.Contains(Asset)) { Result->SetBoolField(TEXT("reference_only"), true); return Result; }
                Expanded.Add(Asset);
                auto Fields = MakeShared<FJsonObject>();
                if (Depth > 12 || Expanded.Num() > 256) { Missing(Path, TEXT("Object expansion bound exceeded")); return Result; }
                if ((Asset->IsA<UInputTrigger>() || Asset->IsA<UInputModifier>() || Asset->IsA<UPlayerMappableKeySettings>())
                    && Asset->GetClass()->GetPackage()->GetName() != TEXT("/Script/EnhancedInput"))
                    Missing(Path, TEXT("Custom input child class may contain unreflected native data; reflected fields below are partial"));
                TArray<FProperty*> Properties;
                for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
                    if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) Properties.Add(*Field);
                Properties.Sort([](const FProperty& Left, const FProperty& Right) { return Left.GetName() < Right.GetName(); });
                for (auto* Property : Properties)
                    Fields->SetField(Property->GetName(), Value(Property, Property->ContainerPtrToValuePtr<void>(Asset), Path + TEXT(".") + Property->GetName(), Depth + 1));
                Result->SetObjectField(TEXT("fields"), Fields); return Result;
            }

            TSharedPtr<FJsonValue> Value(const FProperty* Property, const void* Data, const FString& Path, int32 Depth = 0)
            {
                if (++Values > 50000 || Depth > 16) return Missing(Path, TEXT("Snapshot value/depth bound exceeded"));
                if (Property->ArrayDim != 1) return Missing(Path, TEXT("Static array unsupported"));
                if (const auto* Bool = CastField<FBoolProperty>(Property)) return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Data));
                if (const auto* Enum = CastField<FEnumProperty>(Property))
                    return MakeShared<FJsonValueString>(Enum->GetEnum()->GetNameStringByValue(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Data)));
                if (const auto* Byte = CastField<FByteProperty>(Property)) if (Byte->Enum) return MakeShared<FJsonValueString>(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(Data)));
                if (const auto* Number = CastField<FNumericProperty>(Property))
                {
                    const bool Unsigned = CastField<FUInt16Property>(Property) || CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property) || CastField<FByteProperty>(Property);
                    const double Numeric = Number->IsFloatingPoint() ? Number->GetFloatingPointPropertyValue(Data)
                        : Unsigned ? static_cast<double>(Number->GetUnsignedIntPropertyValue(Data)) : static_cast<double>(Number->GetSignedIntPropertyValue(Data));
                    if (!FMath::IsFinite(Numeric) || (Number->IsInteger() && FMath::Abs(Numeric) > 9007199254740991.0)) return Missing(Path, TEXT("Nonfinite or non-lossless number"));
                    return MakeShared<FJsonValueNumber>(Numeric);
                }
                if (const auto* Name = CastField<FNameProperty>(Property)) return String(Name->GetPropertyValue(Data).ToString(), Path);
                if (const auto* TextString = CastField<FStrProperty>(Property)) return String(TextString->GetPropertyValue(Data), Path);
                if (const auto* Text = CastField<FTextProperty>(Property))
                {
                    auto Result = MakeShared<FJsonObject>(); FString Serialized;
                    FTextStringHelper::WriteToBuffer(Serialized, Text->GetPropertyValue(Data));
                    Result->SetField(TEXT("text"), String(Text->GetPropertyValue(Data).ToString(), Path));
                    Result->SetField(TEXT("identity"), String(Serialized, Path)); return MakeShared<FJsonValueObject>(Result);
                }
                if (const auto* Soft = CastField<FSoftObjectProperty>(Property))
                {
                    const auto Reference = Soft->GetPropertyValue(Data);
                    if (Reference.IsNull()) return MakeShared<FJsonValueNull>();
                    return MakeShared<FJsonValueString>(Reference.ToString());
                }
                if (const auto* Reference = CastField<FObjectPropertyBase>(Property))
                {
                    const auto* Target = Reference->GetObjectPropertyValue(Data);
                    if (!Target) return MakeShared<FJsonValueNull>();
                    if (Target->IsA<UInputTrigger>() || Target->IsA<UInputModifier>() || Target->IsA<UPlayerMappableKeySettings>())
                        return MakeShared<FJsonValueObject>(Object(Target, Path, Depth + 1));
                    auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("path"), Target->GetPathName());
                    Result->SetStringField(TEXT("class_path"), Target->GetClass()->GetPathName());
                    if (!Target->IsAsset() && !Target->IsA<UClass>()) Missing(Path, TEXT("Unexpanded non-asset object type"));
                    return MakeShared<FJsonValueObject>(Result);
                }
                if (const auto* Structure = CastField<FStructProperty>(Property))
                {
                    if (Structure->Struct == FKey::StaticStruct()) return MakeShared<FJsonValueString>(static_cast<const FKey*>(Data)->GetFName().ToString());
                    if (Structure->Struct == FInputActionValue::StaticStruct())
                    {
                        const auto& Input = *static_cast<const FInputActionValue*>(Data);
                        const FVector Vector = Input.Get<FVector>();
                        if (!FMath::IsFinite(Vector.X) || !FMath::IsFinite(Vector.Y) || !FMath::IsFinite(Vector.Z)) return Missing(Path, TEXT("Nonfinite InputActionValue"));
                        auto Result = MakeShared<FJsonObject>();
                        Result->SetStringField(TEXT("value_type"), StaticEnum<EInputActionValueType>()->GetNameStringByValue(static_cast<int64>(Input.GetValueType())));
                        Result->SetArrayField(TEXT("value"), {MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y), MakeShared<FJsonValueNumber>(Vector.Z)});
                        return MakeShared<FJsonValueObject>(Result);
                    }
                    auto Result = MakeShared<FJsonObject>();
                    TArray<FProperty*> Fields;
                    for (TFieldIterator<FProperty> Field(Structure->Struct); Field; ++Field) if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) Fields.Add(*Field);
                    Fields.Sort([](const FProperty& Left, const FProperty& Right) { return Left.GetName() < Right.GetName(); });
                    if (Fields.IsEmpty()) return Missing(Path, TEXT("Struct has no reflected saved fields"));
                    for (auto* Field : Fields) Result->SetField(Field->GetName(), Value(Field, Field->ContainerPtrToValuePtr<void>(Data), Path + TEXT(".") + Field->GetName(), Depth + 1));
                    return MakeShared<FJsonValueObject>(Result);
                }
                if (const auto* Array = CastField<FArrayProperty>(Property))
                {
                    FScriptArrayHelper Helper(Array, Data); TArray<TSharedPtr<FJsonValue>> Items;
                    if (Helper.Num() > 4096) return Missing(Path, TEXT("Array exceeds 4096 elements"));
                    for (int32 Index = 0; Index < Helper.Num(); ++Index)
                    {
                        if (Values >= 50000) { Missing(Path, TEXT("Snapshot value budget exceeded")); break; }
                        Items.Add(Value(Array->Inner, Helper.GetRawPtr(Index), Path + FString::Printf(TEXT("[%d]"), Index), Depth + 1));
                    }
                    return MakeShared<FJsonValueArray>(Items);
                }
                if (const auto* Map = CastField<FMapProperty>(Property))
                {
                    FScriptMapHelper Helper(Map, Data); TArray<TSharedPtr<FJsonValue>> Items;
                    if (Helper.Num() > 1024) return Missing(Path, TEXT("Map exceeds 1024 entries"));
                    for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index))
                    {
                        if (Values >= 50000) { Missing(Path, TEXT("Snapshot value budget exceeded")); break; }
                        auto Entry = MakeShared<FJsonObject>();
                        Entry->SetField(TEXT("key"), Value(Map->KeyProp, Helper.GetKeyPtr(Index), Path + TEXT(".key"), Depth + 1));
                        Entry->SetField(TEXT("value"), Value(Map->ValueProp, Helper.GetValuePtr(Index), Path + TEXT(".value"), Depth + 1));
                        Items.Add(MakeShared<FJsonValueObject>(Entry));
                    }
                    Items.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right) { return Json(Left->AsObject()->Values[TEXT("key")]) < Json(Right->AsObject()->Values[TEXT("key")]); });
                    return MakeShared<FJsonValueArray>(Items);
                }
                return Missing(Path, TEXT("Unsupported saved property type: ") + Property->GetCPPType());
            }
        };

        TSharedRef<FJsonObject> Snapshot(const UInputMappingContext* Asset, const TArray<FEnhancedActionKeyMapping>* ProposedDefaults = nullptr)
        {
            auto Result = MakeShared<FJsonObject>();
            Result->SetNumberField(TEXT("mapping_contract"), 1);
            Result->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
            Result->SetStringField(TEXT("object_path"), Asset->GetPathName());
            Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
            FSnapshotReader Reader;
            TMap<FString, const UInputAction*> Actions;
            auto Rows = [&Reader, &Actions](const TArray<FEnhancedActionKeyMapping>& Mappings, const FString& Scope, const FString& Prefix)
            {
                TArray<TSharedPtr<FJsonValue>> Values;
                if (Mappings.Num() > 4096) Reader.Missing(Prefix, TEXT("Mapping count exceeds 4096"));
                auto* Behavior = FEnhancedActionKeyMapping::StaticStruct()->FindPropertyByName(TEXT("SettingBehavior"));
                auto* Settings = FindFProperty<FObjectPropertyBase>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("PlayerMappableKeySettings"));
                for (int32 Index = 0; Index < FMath::Min(Mappings.Num(), 4096); ++Index)
                {
                    if (++Reader.Rows > 4096) { Reader.Missing(Prefix, TEXT("Total mapping count exceeds 4096")); break; }
                    const auto& Mapping = Mappings[Index];
                    const FString Path = Prefix + FString::Printf(TEXT("[%d]"), Index);
                    const int32 UnsupportedBefore = Reader.Failures;
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetStringField(TEXT("scope"), Scope); Row->SetNumberField(TEXT("index"), Index);
                    if (Mapping.Action) { Row->SetStringField(TEXT("action_path"), Mapping.Action->GetPathName()); Actions.Add(Mapping.Action->GetPathName(), Mapping.Action); }
                    else Row->SetField(TEXT("action_path"), MakeShared<FJsonValueNull>());
                    Row->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());
                    Row->SetBoolField(TEXT("key_valid"), Mapping.Key.IsValid());
                    Row->SetNumberField(TEXT("trigger_count"), Mapping.Triggers.Num());
                    Row->SetNumberField(TEXT("modifier_count"), Mapping.Modifiers.Num());
                    TArray<TSharedPtr<FJsonValue>> Triggers, Modifiers;
                    if (Mapping.Triggers.Num() > 256 || Mapping.Modifiers.Num() > 256) Reader.Missing(Path, TEXT("Trigger/modifier array bound exceeded"));
                    else
                    {
                        for (const UInputTrigger* Trigger : Mapping.Triggers)
                        { if (Trigger) Triggers.Add(MakeShared<FJsonValueObject>(Reader.Object(Trigger, Path + TEXT(".triggers")))); else Triggers.Add(MakeShared<FJsonValueNull>()); }
                        for (const UInputModifier* Modifier : Mapping.Modifiers)
                        { if (Modifier) Modifiers.Add(MakeShared<FJsonValueObject>(Reader.Object(Modifier, Path + TEXT(".modifiers")))); else Modifiers.Add(MakeShared<FJsonValueNull>()); }
                    }
                    Row->SetArrayField(TEXT("triggers"), Triggers); Row->SetArrayField(TEXT("modifiers"), Modifiers);
                    if (Behavior) Row->SetField(TEXT("setting_behavior"), Reader.Value(Behavior, Behavior->ContainerPtrToValuePtr<void>(&Mapping), Path + TEXT(".SettingBehavior")));
                    else Reader.Missing(Path, TEXT("SettingBehavior unavailable in this engine"));
                    const auto* Owned = Settings ? Settings->GetObjectPropertyValue_InContainer(&Mapping) : nullptr;
                    Row->SetBoolField(TEXT("has_own_mappable_settings"), Owned != nullptr);
                    if (Owned) Row->SetObjectField(TEXT("own_mappable_settings"), Reader.Object(Owned, Path + TEXT(".settings")));
                    else Row->SetField(TEXT("own_mappable_settings"), MakeShared<FJsonValueNull>());
                    if (!Settings) Reader.Missing(Path, TEXT("Owned mappable field unavailable"));
                    Row->SetBoolField(TEXT("complete"), Reader.Failures == UnsupportedBefore);
                    Values.Add(MakeShared<FJsonValueObject>(Row));
                }
                return Values;
            };
            const auto& Defaults = ProposedDefaults ? *ProposedDefaults : Asset->GetMappings();
            Result->SetArrayField(TEXT("default_mappings"), Rows(Defaults, TEXT("default"), TEXT("default_mappings")));
            Result->SetNumberField(TEXT("default_mapping_count"), Defaults.Num());
            TArray<TSharedPtr<FJsonValue>> Profiles;
            auto Names = Asset->GetProfilesWithOverridenMappings(); Names.Sort();
            if (Names.Num() > 1024) Reader.Missing(TEXT("profile_overrides"), TEXT("Profile count exceeds 1024"));
            for (int32 ProfileIndex = 0; ProfileIndex < FMath::Min(Names.Num(), 1024); ++ProfileIndex)
            {
                const FString& Name = Names[ProfileIndex];
                auto Profile = MakeShared<FJsonObject>(); Profile->SetStringField(TEXT("profile_id"), Name);
                Profile->SetArrayField(TEXT("mappings"), Rows(Asset->GetMappingsForProfile(Name), TEXT("profile_override"), TEXT("profile[") + Name + TEXT("]")));
                Profile->SetNumberField(TEXT("mapping_count"), Asset->GetMappingsForProfile(Name).Num());
                Profiles.Add(MakeShared<FJsonValueObject>(Profile));
            }
            Result->SetArrayField(TEXT("profile_overrides"), Profiles);
            Result->SetNumberField(TEXT("profile_override_count"), Names.Num());
            TArray<FString> ActionPaths; Actions.GetKeys(ActionPaths); ActionPaths.Sort(); TArray<TSharedPtr<FJsonValue>> ActionValues;
            for (const auto& Path : ActionPaths) ActionValues.Add(MakeShared<FJsonValueObject>(Reader.Object(Actions[Path], Path)));
            Result->SetArrayField(TEXT("actions"), ActionValues);
            auto Context = MakeShared<FJsonObject>();
            for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
            {
                if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || Field->GetFName() == TEXT("DefaultKeyMappings") || Field->GetFName() == TEXT("MappingProfileOverrides")) continue;
                Context->SetField(Field->GetName(), Reader.Value(*Field, Field->ContainerPtrToValuePtr<void>(Asset), TEXT("context.") + Field->GetName()));
            }
            Result->SetObjectField(TEXT("context_settings"), Context);
            const bool Complete = Reader.Failures == 0;
            Result->SetNumberField(TEXT("unsupported_count"), Reader.Failures);
            Result->SetBoolField(TEXT("complete"), Complete); Result->SetArrayField(TEXT("unsupported"), Reader.Unsupported);
            FString Serialized;
            FJsonSerializer::Serialize(Result, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized));
            const auto Bytes = FTCHARToUTF8(*Serialized);
            FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
            if (Complete) Result->SetStringField(TEXT("baseline"), Hash.ToString());
            else Result->SetField(TEXT("baseline"), MakeShared<FJsonValueNull>());
            Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
            Result->SetStringField(TEXT("semantics"), TEXT("Saved default/profile mappings only. Action settings (including triggers/modifiers) are separate and inherited; no active profile resolution or LocalPlayer mapping application. Indices require this baseline."));
            return Result;
        }

        bool Simple(const FEnhancedActionKeyMapping& Mapping)
        {
            const auto* Behavior = FindFProperty<FEnumProperty>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("SettingBehavior"));
            const auto* Settings = FindFProperty<FObjectPropertyBase>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("PlayerMappableKeySettings"));
            return Behavior && Settings && Mapping.Triggers.IsEmpty() && Mapping.Modifiers.IsEmpty()
                && !Settings->GetObjectPropertyValue_InContainer(&Mapping)
                && Behavior->GetUnderlyingProperty()->GetSignedIntPropertyValue(Behavior->ContainerPtrToValuePtr<void>(&Mapping))
                    == static_cast<int64>(EPlayerMappableKeySettingBehaviors::InheritSettingsFromAction);
        }

#if WITH_DEV_AUTOMATION_TESTS
        bool FailSaveForTest = false;
#endif
        bool Save(UInputMappingContext* Asset)
        {
#if WITH_DEV_AUTOMATION_TESTS
            if (FailSaveForTest) return false;
#endif
            return UEditorAssetLibrary::SaveLoadedAsset(Asset, false) && !Asset->GetPackage()->IsDirty();
        }
    }

    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error;
        auto* Asset = Load(Params, Error);
        if (!Asset) return Failure(Error);
        auto Result = Snapshot(Asset);
        Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        return Result;
    }

    TSharedPtr<FJsonObject> Append(const TSharedPtr<FJsonObject>& Params)
    {
        if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask
            || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid()))
            return Failure(TEXT("Requires an idle Editor outside PIE/save/GC/slow tasks/modal windows"));
        if (!Params) return Failure(TEXT("Missing arguments"));
        for (const auto& Pair : Params->Values)
            if (Pair.Key != TEXT("asset_path") && Pair.Key != TEXT("expected_baseline") && Pair.Key != TEXT("mappings")
                && Pair.Key != TEXT("dry_run") && Pair.Key != TEXT("save")) return Failure(TEXT("Unknown argument: ") + Pair.Key);
        FString Error;
        auto* Asset = Load(Params, Error);
        if (!Asset) return Failure(Error);
        const auto Before = Snapshot(Asset);
        if (!Before->GetBoolField(TEXT("complete"))) return Failure(TEXT("IMC has incomplete/unsupported snapshot; no edits permitted"));
        FString Expected;
        if (!Params->TryGetStringField(TEXT("expected_baseline"), Expected) || Expected != Before->GetStringField(TEXT("baseline")))
            return Failure(TEXT("Missing/stale expected_baseline; read and review this exact IMC again"));
        bool DryRun = true, SaveAsset = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), SaveAsset)) return Failure(TEXT("save must be bool"));
        if (Asset->GetPackage()->IsDirty()) return Failure(TEXT("Preexisting dirty IMC refused; explicitly save the reviewed draft separately"));
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Params->TryGetArrayField(TEXT("mappings"), Entries) || Entries->IsEmpty() || Entries->Num() > 64)
            return Failure(TEXT("mappings requires 1..64 {action_path,key} objects for default scope only"));
        TSet<FString> Requested;
        TArray<FEnhancedActionKeyMapping> Additions;
        TArray<TSharedPtr<FJsonValue>> AddedIndices, ExistingIndices;
        for (int32 Index = 0; Index < Entries->Num(); ++Index)
        {
            const FString Prefix = FString::Printf(TEXT("mappings[%d]: "), Index);
            const auto& Entry = (*Entries)[Index]; FString ActionPath, KeyName;
            if (!Entry || Entry->Type != EJson::Object || Entry->AsObject()->Values.Num() != 2
                || !Entry->AsObject()->TryGetStringField(TEXT("action_path"), ActionPath) || !Entry->AsObject()->TryGetStringField(TEXT("key"), KeyName)
                || !ActionPath.StartsWith(TEXT("/Game/")) || ActionPath.Len() >= NAME_SIZE || ActionPath.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(ActionPath))
                return Failure(Prefix + TEXT("Require an exact /Game InputAction package action_path and internal key name; no extra fields"));
            if (KeyName.IsEmpty() || KeyName.Len() >= NAME_SIZE || KeyName.TrimStartAndEnd() != KeyName)
                return Failure(Prefix + TEXT("Invalid internal key name length/whitespace"));
            const FKey Key{FName(*KeyName)};
            if (!Key.IsValid()
                || Key.IsDeprecated() || Key.GetFName().ToString() != KeyName || Key == EKeys::AnyKey)
                return Failure(Prefix + TEXT("Invalid/deprecated key or noncanonical internal key name"));
            const auto* Action = Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ActionPath));
            if (!Action || Action->GetPackage()->GetName() != ActionPath || Action->GetPackage()->IsDirty()) return Failure(Prefix + TEXT("Require an existing saved canonical InputAction, not a redirected path, another asset or dirty action"));
            const FString Identity = Action->GetPathName() + TEXT("|") + Key.GetFName().ToString();
            if (Requested.Contains(Identity)) return Failure(Prefix + TEXT("Duplicate action/key in request; batch refused, not silently deduplicated"));
            Requested.Add(Identity);
            int32 Matches = 0, MatchIndex = INDEX_NONE;
            const auto& Defaults = Asset->GetMappings();
            for (int32 Row = 0; Row < Defaults.Num(); ++Row) if (Defaults[Row].Action == Action && Defaults[Row].Key == Key) { ++Matches; MatchIndex = Row; }
            if (Matches > 1 || (Matches == 1 && !Simple(Defaults[MatchIndex])))
                return Failure(Prefix + TEXT("Existing action/key is ambiguous or has non-simple settings; no first-match, overwrite or duplicate append"));
            if (Matches == 1) ExistingIndices.Add(MakeShared<FJsonValueNumber>(MatchIndex));
            else { AddedIndices.Add(MakeShared<FJsonValueNumber>(Defaults.Num() + Additions.Num())); Additions.Emplace(Action, Key); }
        }
        auto ProposedRows = Asset->GetMappings(); ProposedRows.Append(Additions);
        const auto Proposed = Snapshot(Asset, &ProposedRows);
        if (!Proposed->GetBoolField(TEXT("complete"))) return Failure(TEXT("Proposed mapping/action settings incomplete; review unsupported fields before authoring"));
        auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("mapping_contract"), 1);
        Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("dry_run"), DryRun);
        Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("would_modify"), !Additions.IsEmpty()); Result->SetBoolField(TEXT("package_dirty"), false);
        Result->SetStringField(TEXT("scope"), TEXT("default")); Result->SetObjectField(TEXT("before"), Before);
        Result->SetObjectField(TEXT("proposed"), Proposed); Result->SetObjectField(TEXT("after"), Before);
        Result->SetArrayField(TEXT("added_indices"), AddedIndices); Result->SetArrayField(TEXT("existing_indices"), ExistingIndices);
        if (DryRun || Additions.IsEmpty()) return Result;
        auto* Property = FindFProperty<FStructProperty>(Asset->GetClass(), TEXT("DefaultKeyMappings"));
        if (!Property || Property->Struct != FInputMappingContextMappingData::StaticStruct()) return Failure(TEXT("Formal DefaultKeyMappings structure unavailable; no deprecated fallback"));
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "AppendEnhancedInputMappings", "Append Enhanced Input default mappings"));
            Asset->Modify(); Asset->PreEditChange(Property);
            auto* Data = Property->ContainerPtrToValuePtr<FInputMappingContextMappingData>(Asset);
            Data->Mappings.Append(Additions);
            FPropertyChangedEvent Event(Property, EPropertyChangeType::ArrayAdd); Asset->PostEditChangeProperty(Event);
            Asset->MarkPackageDirty();
        }
        Result->SetBoolField(TEXT("modified"), true);
        auto After = Snapshot(Asset);
        if (!After->GetBoolField(TEXT("complete")) || After->GetStringField(TEXT("baseline")) != Proposed->GetStringField(TEXT("baseline")))
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback"));
            Result->SetStringField(TEXT("error"), TEXT("Actual stored rows/settings differ from validated proposal; modified state remains, not saved"));
        }
        else if (SaveAsset && !Save(Asset))
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save"));
            Result->SetStringField(TEXT("error"), TEXT("Mapping changes applied but save failed; inspect actual after/dirty state, no rollback or retry"));
        }
        else if (SaveAsset) Result->SetBoolField(TEXT("saved"), true);
        Result->SetObjectField(TEXT("after"), Snapshot(Asset)); Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
        return Result;
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPEnhancedInputReadTest, "UnrealMCP.EnhancedInput.Read",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEnhancedInputReadTest::RunTest(const FString& Parameters)
{
    auto* Asset = NewObject<UInputMappingContext>();
    auto* Action = NewObject<UInputAction>(Asset);
    auto* Default = FindFProperty<FStructProperty>(Asset->GetClass(), TEXT("DefaultKeyMappings"));
    if (!TestNotNull(TEXT("UE5.7 formal default structure"), Default)) return false;
    auto* Data = Default->ContainerPtrToValuePtr<FInputMappingContextMappingData>(Asset);
    Data->Mappings.Emplace(Action, EKeys::Escape);
    Action->bTriggerWhenPaused = true;
    auto* Trigger = NewObject<UInputTriggerHold>(Asset); Trigger->HoldTimeThreshold = 0.75f;
    auto* Modifier = NewObject<UInputModifierNegate>(Asset);
    Data->Mappings[0].Triggers.Add(Trigger); Data->Mappings[0].Modifiers.Add(Modifier);
    auto* ProfileProperty = FindFProperty<FMapProperty>(Asset->GetClass(), TEXT("MappingProfileOverrides"));
    auto* Profiles = ProfileProperty->ContainerPtrToValuePtr<TMap<FString, FInputMappingContextMappingData>>(Asset);
    Profiles->Add(TEXT("TestProfile")).Mappings.Emplace(Action, EKeys::Gamepad_FaceButton_Right);
    Profiles->Add(TEXT("EmptyProfile"));
    const bool Dirty = Asset->GetPackage()->IsDirty();
    const auto Before = UnrealMCPEnhancedInput::Snapshot(Asset);
    if (!TestTrue(TEXT("Complex default and profile rows read complete"), Before->GetBoolField(TEXT("complete"))))
    { AddError(UnrealMCPEnhancedInput::Json(MakeShared<FJsonValueObject>(Before))); return false; }
    TestEqual(TEXT("Formal mapping array returned"), Before->GetArrayField(TEXT("default_mappings")).Num(), 1);
    TestEqual(TEXT("Key uses internal name"), Before->GetArrayField(TEXT("default_mappings"))[0]->AsObject()->GetStringField(TEXT("key")), FString(TEXT("Escape")));
    TestEqual(TEXT("Deterministic baseline"), UnrealMCPEnhancedInput::Snapshot(Asset)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    TestEqual(TEXT("Read preserves dirty state"), Asset->GetPackage()->IsDirty(), Dirty);
    TestEqual(TEXT("All profile scopes including empty retained"), Before->GetArrayField(TEXT("profile_overrides")).Num(), 2);
    const auto Row = Before->GetArrayField(TEXT("default_mappings"))[0]->AsObject();
    TestEqual(TEXT("Trigger order/object preserved"), Row->GetArrayField(TEXT("triggers"))[0]->AsObject()->GetStringField(TEXT("path")), Trigger->GetPathName());
    TestEqual(TEXT("Modifier presence preserved"), Row->GetArrayField(TEXT("modifiers")).Num(), 1);
    Trigger->HoldTimeThreshold = 0.5f;
    TestNotEqual(TEXT("Nested object value changes baseline"), UnrealMCPEnhancedInput::Snapshot(Asset)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Trigger->HoldTimeThreshold = 0.75f;
    Data->Mappings[0].bShouldBeIgnored = true;
    TestEqual(TEXT("Transient runtime flags excluded"), UnrealMCPEnhancedInput::Snapshot(Asset)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Modifier->bX = false;
    const auto ChangedModifier = UnrealMCPEnhancedInput::Snapshot(Asset);
    TestNotEqual(TEXT("Modifier field participates in baseline"), ChangedModifier->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Action->Modifiers.Add(nullptr);
    const auto Inherited = UnrealMCPEnhancedInput::Snapshot(Asset);
    TestFalse(TEXT("Action modifier presence is separate from row"), Inherited->GetArrayField(TEXT("actions"))[0]->AsObject()->GetObjectField(TEXT("fields"))->GetArrayField(TEXT("Modifiers")).IsEmpty());
    Data->Mappings[0].Modifiers.SetNum(257);
    const auto Bounded = UnrealMCPEnhancedInput::Snapshot(Asset);
    TestFalse(TEXT("Oversized children are incomplete, not equivalent empty rows"), Bounded->GetBoolField(TEXT("complete")));
    TestTrue(TEXT("Incomplete snapshot has no usable baseline"), Bounded->Values[TEXT("baseline")]->IsNull());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPEnhancedInputAppendTest, "UnrealMCP.EnhancedInput.Append",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEnhancedInputAppendTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPEnhancedInput;
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPEnhancedInputRoot="), Root)) Root = TEXT("/Game/__Dev/EnhancedInput_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!Root.StartsWith(TEXT("/Game/__Dev/")) || !FPackageName::IsValidLongPackageName(Root) || FPackageName::DoesPackageExist(Root + TEXT("/IMC_Menu")))
    { AddError(TEXT("Require a fresh /Game/__Dev fixture root")); return false; }
    auto NewAsset = [&Root](UClass* Class, const FString& Name)
    {
        auto* Package = CreatePackage(*(Root + TEXT("/") + Name));
        auto* Asset = NewObject<UObject>(Package, Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Asset); Asset->MarkPackageDirty(); return Asset;
    };
    auto* Back = Cast<UInputAction>(NewAsset(UInputAction::StaticClass(), TEXT("IA_Back")));
    auto* Accept = Cast<UInputAction>(NewAsset(UInputAction::StaticClass(), TEXT("IA_Accept")));
    auto* Context = Cast<UInputMappingContext>(NewAsset(UInputMappingContext::StaticClass(), TEXT("IMC_Menu")));
    for (auto* Action : {Back, Accept})
    {
        Action->ValueType = EInputActionValueType::Boolean; Action->bTriggerWhenPaused = true; Action->bConsumeInput = true;
        if (!TestTrue(TEXT("Fixture action saved"), UEditorAssetLibrary::SaveLoadedAsset(Action, false))) return false;
    }
    if (!TestTrue(TEXT("Empty fixture IMC saved"), Save(Context))) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/IMC_Menu"));
    TArray<TSharedPtr<FJsonValue>> Mappings;
    for (const auto& Pair : TArray<TPair<UInputAction*, FKey>>{{Back, EKeys::Escape}, {Back, EKeys::Gamepad_FaceButton_Right},
        {Accept, EKeys::Enter}, {Accept, EKeys::SpaceBar}, {Accept, EKeys::Gamepad_FaceButton_Bottom}})
    {
        auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("action_path"), Pair.Key->GetPackage()->GetName());
        Row->SetStringField(TEXT("key"), Pair.Value.GetFName().ToString()); Mappings.Add(MakeShared<FJsonValueObject>(Row));
    }
    Params->SetArrayField(TEXT("mappings"), Mappings);
    auto Refresh = [&]() { const auto Readback = Read(Params); Params->SetStringField(TEXT("expected_baseline"), Readback->GetStringField(TEXT("baseline"))); return Readback; };
    const auto Before = Refresh();
    const FString ContextFile = FPackageName::LongPackageNameToFilename(Root + TEXT("/IMC_Menu"), FPackageName::GetAssetPackageExtension());
    const auto OriginalHash = FMD5Hash::HashFile(*ContextFile);
    const auto Preview = Append(Params);
    if (!TestTrue(TEXT("Five-row complete preview"), Preview->GetBoolField(TEXT("success")))) { AddError(Json(MakeShared<FJsonValueObject>(Preview))); return false; }
    TestFalse(TEXT("Preview does not modify"), Preview->GetBoolField(TEXT("modified")));
    TestEqual(TEXT("Proposal contains five distinct ordered rows"), Preview->GetObjectField(TEXT("proposed"))->GetArrayField(TEXT("default_mappings")).Num(), 5);
    TestEqual(TEXT("Preview baseline unchanged"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("dry_run"), false);
    const auto Draft = Append(Params);
    if (!TestTrue(TEXT("Five-row draft applied"), Draft->GetBoolField(TEXT("success")))) return false;
    TestTrue(TEXT("Draft is modified/unsaved/dirty"), Draft->GetBoolField(TEXT("modified")) && !Draft->GetBoolField(TEXT("saved")) && Context->GetPackage()->IsDirty());
    TestTrue(TEXT("save=false leaves disk unchanged"), FMD5Hash::HashFile(*ContextFile) == OriginalHash);
    Refresh(); TestFalse(TEXT("Dirty IMC refuses apply"), Append(Params)->GetBoolField(TEXT("success")));
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo removes all five atomically"), Context->GetMappings().Num(), 0);
    GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores five rows"), Context->GetMappings().Num(), 5);
    GEditor->UndoTransaction(); Context->GetPackage()->SetDirtyFlag(false); Refresh();
    Params->SetBoolField(TEXT("save"), true);
    const auto Applied = Append(Params);
    if (!TestTrue(TEXT("Five rows really saved"), Applied->GetBoolField(TEXT("success")) && Applied->GetBoolField(TEXT("saved")))) return false;
    Refresh();
    const auto Repeat = Append(Params);
    TestTrue(TEXT("Identical batch is no-op"), Repeat->GetBoolField(TEXT("success")) && !Repeat->GetBoolField(TEXT("modified")) && !Repeat->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Repeated batch never doubles"), Context->GetMappings().Num(), 5);
    auto Invalid = MakeShared<FJsonObject>(); Invalid->SetStringField(TEXT("action_path"), Root + TEXT("/IA_Back")); Invalid->SetStringField(TEXT("key"), TEXT("NotARealKey"));
    auto ValidNew = MakeShared<FJsonObject>(); ValidNew->SetStringField(TEXT("action_path"), Root + TEXT("/IA_Back")); ValidNew->SetStringField(TEXT("key"), TEXT("Tab"));
    auto RejectUnchanged = [&](const FString& Label)
    {
        const auto Baseline = Read(Params)->GetStringField(TEXT("baseline")); const auto Result = Append(Params);
        TestFalse(Label, Result->GetBoolField(TEXT("success"))); TestFalse(TEXT("Rejection reports no modification"), Result->GetBoolField(TEXT("modified")));
        TestFalse(TEXT("Rejection preserves dirty false"), Context->GetPackage()->IsDirty());
        TestEqual(TEXT("Rejected batch baseline unchanged"), Read(Params)->GetStringField(TEXT("baseline")), Baseline);
    };
    Params->SetArrayField(TEXT("mappings"), {MakeShared<FJsonValueObject>(ValidNew), MakeShared<FJsonValueObject>(Invalid)}); RejectUnchanged(TEXT("Invalid final key rejects first valid row too"));
    Invalid->SetStringField(TEXT("key"), FString::ChrN(NAME_SIZE + 8, TCHAR('X'))); RejectUnchanged(TEXT("Overlong key refused before FName construction"));
    Invalid->SetStringField(TEXT("key"), TEXT("Tab")); Invalid->SetStringField(TEXT("action_path"), Root + TEXT("/IMC_Menu")); RejectUnchanged(TEXT("Non-InputAction refused"));
    Invalid->SetStringField(TEXT("action_path"), TEXT("IA_Back")); RejectUnchanged(TEXT("Short action path refused"));
    Params->SetArrayField(TEXT("mappings"), {MakeShared<FJsonValueObject>(ValidNew), MakeShared<FJsonValueObject>(ValidNew)}); RejectUnchanged(TEXT("Duplicate request explicitly rejected"));
    Params->SetArrayField(TEXT("mappings"), {MakeShared<FJsonValueObject>(ValidNew)});
    Params->SetStringField(TEXT("expected_baseline"), TEXT("stale")); RejectUnchanged(TEXT("Stale baseline refused")); Refresh();
    auto* Default = FindFProperty<FStructProperty>(Context->GetClass(), TEXT("DefaultKeyMappings"))->ContainerPtrToValuePtr<FInputMappingContextMappingData>(Context);
    Default->Mappings.Emplace(Back, EKeys::Escape); Context->MarkPackageDirty(); Save(Context); Refresh();
    Params->SetArrayField(TEXT("mappings"), {Mappings[0]}); RejectUnchanged(TEXT("Multiple identical action/key candidates refused"));
    Default->Mappings.Pop();
    auto* Trigger = NewObject<UInputTriggerHold>(Context, TEXT("PreservedHold"), RF_Transactional); Trigger->HoldTimeThreshold = 0.4f;
    auto* Modifier = NewObject<UInputModifierNegate>(Context, TEXT("PreservedNegate"), RF_Transactional);
    auto* Settings = NewObject<UPlayerMappableKeySettings>(Context, TEXT("PreservedMappable"), RF_Transactional);
    FindFProperty<FNameProperty>(Settings->GetClass(), TEXT("Name"))->SetPropertyValue_InContainer(Settings, FName(TEXT("PreservedBack")));
    auto& Complex = Default->Mappings[0]; Complex.Triggers.Add(Trigger); Complex.Modifiers.Add(Modifier);
    auto* Behavior = FindFProperty<FEnumProperty>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("SettingBehavior"));
    Behavior->GetUnderlyingProperty()->SetIntPropertyValue(Behavior->ContainerPtrToValuePtr<void>(&Complex), static_cast<uint64>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
    FindFProperty<FObjectPropertyBase>(FEnhancedActionKeyMapping::StaticStruct(), TEXT("PlayerMappableKeySettings"))->SetObjectPropertyValue_InContainer(&Complex, Settings);
    auto* Profiles = FindFProperty<FMapProperty>(Context->GetClass(), TEXT("MappingProfileOverrides"))->ContainerPtrToValuePtr<TMap<FString, FInputMappingContextMappingData>>(Context);
    Profiles->Add(TEXT("PreservedProfile")).Mappings.Add(Complex); Profiles->Add(TEXT("EmptyProfile"));
    Context->MarkPackageDirty(); if (!TestTrue(TEXT("Complex fixture persisted"), Save(Context))) return false;
    const auto ComplexBefore = Refresh();
    if (!TestTrue(TEXT("All standard instanced settings readable"), ComplexBefore->GetBoolField(TEXT("complete")))) { AddError(Json(MakeShared<FJsonValueObject>(ComplexBefore))); return false; }
    Params->SetArrayField(TEXT("mappings"), {Mappings[0]}); RejectUnchanged(TEXT("Same pair complex row never treated as simple no-op"));
    Params->SetArrayField(TEXT("mappings"), {MakeShared<FJsonValueObject>(ValidNew)});
    const auto BeforeFailedSaveHash = FMD5Hash::HashFile(*ContextFile);
    FailSaveForTest = true; const auto FailedSave = Append(Params); FailSaveForTest = false;
    TestTrue(TEXT("Save failure retains applied modification"), FailedSave->GetBoolField(TEXT("modified")));
    TestFalse(TEXT("Save failure never claims saved"), FailedSave->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("Save failure leaves actual disk unchanged"), BeforeFailedSaveHash == FMD5Hash::HashFile(*ContextFile));
    TestEqual(TEXT("Save failure stage"), FailedSave->GetStringField(TEXT("stage")), FString(TEXT("save")));
    TestEqual(TEXT("Old trigger pointer preserved"), Context->GetMappings()[0].Triggers[0].Get(), static_cast<UInputTrigger*>(Trigger));
    TestEqual(TEXT("Old modifier pointer preserved"), Context->GetMappings()[0].Modifiers[0].Get(), static_cast<UInputModifier*>(Modifier));
    TestEqual(TEXT("Old owned settings pointer preserved"), Context->GetMappings()[0].GetPlayerMappableKeySettings(), Settings);
    TestEqual(TEXT("Profile mappings/child settings not changed"), Json(MakeShared<FJsonValueArray>(Snapshot(Context)->GetArrayField(TEXT("profile_overrides")))), Json(MakeShared<FJsonValueArray>(ComplexBefore->GetArrayField(TEXT("profile_overrides")))));
    TestTrue(TEXT("Explicit fixture recovery save"), Save(Context));
    const auto Final = Read(Params);
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("EnhancedInputAcceptance") / FPackageName::GetShortName(Root) / TEXT("snapshot.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt), true);
    TestTrue(TEXT("Full cold-read oracle saved"), FFileHelper::SaveStringToFile(Json(MakeShared<FJsonValueObject>(Final)), *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("EnhancedInput fixture root: ") + Root);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPEnhancedInputReloadTest, "UnrealMCP.EnhancedInput.Reload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEnhancedInputReloadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPEnhancedInput;
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPEnhancedInputReload="), Root))
    { AddInfo(TEXT("Explicit isolated reload root required; cold read not exercised in this batch")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/"))) { AddError(TEXT("Reload restricted to __Dev fixtures")); return false; }
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/IMC_Menu"));
    FString Expected;
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("EnhancedInputAcceptance") / FPackageName::GetShortName(Root) / TEXT("snapshot.json");
    if (!TestTrue(TEXT("Cold oracle exists"), FFileHelper::LoadFileToString(Expected, *Receipt))) return false;
    const auto Readback = Read(Params);
    TestTrue(TEXT("Cold read succeeds"), Readback->GetBoolField(TEXT("success")));
    TestEqual(TEXT("All ordered mappings, profile and child values/ownership persisted"), Json(MakeShared<FJsonValueObject>(Readback)), Expected);
    TestEqual(TEXT("Six rows, not duplicates or deprecated array"), Readback->GetArrayField(TEXT("default_mappings")).Num(), 6);
    TestFalse(TEXT("Cold read not dirty"), Readback->GetBoolField(TEXT("package_dirty")));
    return !HasAnyErrors();
}
#endif