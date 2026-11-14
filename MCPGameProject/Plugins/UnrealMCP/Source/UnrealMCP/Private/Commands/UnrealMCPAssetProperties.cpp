#include "Commands/UnrealMCPAssetProperties.h"
#include "EditorAssetLibrary.h"
#include "Engine/DataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "Internationalization/Text.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Editor.h"
#include "AssetCompilingManager.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectHash.h"

namespace UnrealMCPAssetProperties
{
namespace
{
    constexpr int32 MaxDepth = 8;
    constexpr int32 MaxElements = 1024;
    constexpr int32 MaxValues = 8192;
    constexpr double MaxExactInteger = 9007199254740991.0;

    struct FValueStorage
    {
        FProperty* Property;
        void* Data;

        FValueStorage(FProperty* InProperty, const void* Source) : Property(InProperty)
        {
            Data = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
            Property->InitializeValue(Data);
            if (Source) Property->CopyCompleteValue(Data, Source);
        }
        ~FValueStorage()
        {
            Property->DestroyValue(Data);
            FMemory::Free(Data);
        }
        FValueStorage(const FValueStorage&) = delete;
        FValueStorage& operator=(const FValueStorage&) = delete;
    };

    using FStagedValues = TMap<FProperty*, TSharedPtr<FValueStorage>>;

    struct FConditionContext
    {
        FProperty* Property;
        UStruct* Owner;
        const void* Container;
    };

    struct FResolved
    {
        FProperty* Root = nullptr;
        FProperty* Leaf = nullptr;
        void* Address = nullptr;
        TArray<FConditionContext> Conditions;
        TArray<FProperty*> Chain;
        TArray<TSharedPtr<FJsonValue>> CanonicalPath;
    };

    bool AllowedTargetProperty(UObject* Asset, FProperty* Property)
    {
        if (Asset->IsA<UDataAsset>()) return true;
        static const TSet<FName> PhysicalFields = {TEXT("SurfaceType"), TEXT("Friction"), TEXT("StaticFriction"),
            TEXT("Restitution"), TEXT("Density"), TEXT("FrictionCombineMode"), TEXT("RestitutionCombineMode"),
            TEXT("bOverrideFrictionCombineMode"), TEXT("bOverrideRestitutionCombineMode")};
        return Asset->GetClass() == UPhysicalMaterial::StaticClass() && PhysicalFields.Contains(Property->GetFName());
    }

    bool Editable(FProperty* Property)
    {
        return Property->HasAnyPropertyFlags(CPF_Edit)
            && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient
                | CPF_Deprecated | CPF_InstancedReference | CPF_ContainsInstancedReference);
    }

    bool ConditionMet(const FConditionContext& Context, const FStagedValues* Staged, FString& Error)
    {
        if (!Context.Property->HasMetaData(TEXT("EditCondition"))) return true;
        const FString Expression = Context.Property->GetMetaData(TEXT("EditCondition")).TrimStartAndEnd();
        FString Name = Expression;
        const bool Negated = Name.RemoveFromStart(TEXT("!"));
        Name.TrimStartAndEndInline();
        bool Identifier = !Name.IsEmpty() && (FChar::IsAlpha(Name[0]) || Name[0] == TEXT('_'));
        for (TCHAR Character : Name) Identifier = Identifier && (FChar::IsAlnum(Character) || Character == TEXT('_'));
        FBoolProperty* Toggle = Identifier ? FindFProperty<FBoolProperty>(Context.Owner, *Name) : nullptr;
        if (!Toggle || Toggle->GetName() != Name || Toggle->ArrayDim != 1)
        {
            Error = FString::Printf(TEXT("%s: unsupported EditCondition '%s'; expected a sibling bool or !bool"), *Context.Property->GetName(), *Expression);
            return false;
        }
        const void* Value = Toggle->ContainerPtrToValuePtr<void>(Context.Container);
        if (Staged && Cast<UClass>(Context.Owner))
        {
            if (const auto* Pending = Staged->Find(Toggle)) Value = (*Pending)->Data;
        }
        const bool Enabled = Toggle->GetPropertyValue(Value);
        if (Negated ? !Enabled : Enabled) return true;
        Error = FString::Printf(TEXT("%s: EditCondition '%s' is false; controller '%s' is %s"),
            *Context.Property->GetName(), *Expression, *Name, Enabled ? TEXT("true") : TEXT("false"));
        return false;
    }

    bool DescendantConditions(FProperty* Property, const void* Address, const void* Previous,
        const FStagedValues* Staged, FString& Error, int32& Budget, int32 Depth = 0)
    {
        if (--Budget < 0 || Depth > MaxDepth) { Error = TEXT("Condition traversal exceeds budget"); return false; }
        if (Previous && Property->Identical(Address, Previous)) return true;
        if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Struct->Struct == FGameplayTag::StaticStruct() || Struct->Struct == FGameplayTagContainer::StaticStruct()) return true;
            for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
            {
                const void* CurrentValue = Field->ContainerPtrToValuePtr<void>(Address);
                const void* OldValue = Previous ? Field->ContainerPtrToValuePtr<void>(Previous) : nullptr;
                if (OldValue && Field->Identical(CurrentValue, OldValue)) continue;
                if (!ConditionMet({*Field, Struct->Struct, Address}, Staged, Error)
                    || !DescendantConditions(*Field, CurrentValue, OldValue, Staged, Error, Budget, Depth + 1)) return false;
            }
        }
        else if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper Current(Map, Address);
            if (Current.Num() > MaxElements) { Error = TEXT("Map exceeds 1024 entries"); return false; }
            for (int32 Index = 0; Index < Current.GetMaxIndex(); ++Index)
            {
                if (!Current.IsValidIndex(Index)) continue;
                const void* OldValue = nullptr;
                if (Previous)
                {
                    FScriptMapHelper Old(Map, Previous);
                    const int32 OldIndex = Old.FindMapIndexWithKey(Current.GetKeyPtr(Index));
                    if (OldIndex != INDEX_NONE) OldValue = Old.GetValuePtr(OldIndex);
                }
                if (!DescendantConditions(Map->ValueProp, Current.GetValuePtr(Index), OldValue, Staged, Error, Budget, Depth + 1)) return false;
            }
        }
        else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Current(Array, Address);
            if (Current.Num() > MaxElements) { Error = TEXT("Condition array exceeds 1024 elements"); return false; }
            for (int32 Index = 0; Index < Current.Num(); ++Index)
            {
                const void* OldValue = nullptr;
                if (Previous)
                {
                    FScriptArrayHelper Old(Array, Previous);
                    if (Old.IsValidIndex(Index)) OldValue = Old.GetRawPtr(Index);
                }
                if (!DescendantConditions(Array->Inner, Current.GetRawPtr(Index), OldValue, Staged, Error, Budget, Depth + 1)) return false;
            }
        }
        return true;
    }

    bool ConditionsMet(const FResolved& Resolved, const void* Previous, const FStagedValues* Staged, FString& Error, int32& Budget)
    {
        for (const auto& Condition : Resolved.Conditions)
            if (!ConditionMet(Condition, Staged, Error)) return false;
        return DescendantConditions(Resolved.Leaf, Resolved.Address, Previous, Staged, Error, Budget);
    }

    bool SupportedMapKey(FProperty* Property)
    {
        return Property->HasAnyPropertyFlags(CPF_HasGetValueTypeHash)
            && (CastField<FEnumProperty>(Property) || CastField<FByteProperty>(Property)
                || CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property)
                || (CastField<FNumericProperty>(Property) && CastField<FNumericProperty>(Property)->IsInteger()));
    }

    bool Supported(FProperty* Property, int32 Depth = 0, bool RequireEditableFields = true)
    {
        if (Depth > MaxDepth || Property->ArrayDim != 1
            || Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference)) return false;
        if ((CastField<FObjectProperty>(Property) || CastField<FSoftObjectProperty>(Property))
            && (Property->HasMetaData(TEXT("AllowedClasses")) || Property->HasMetaData(TEXT("DisallowedClasses"))
                || Property->HasMetaData(TEXT("MustImplement")) || Property->HasMetaData(TEXT("ExactClass")))) return false;
        if (FArrayProperty* Array = CastField<FArrayProperty>(Property)) return Supported(Array->Inner, Depth + 1, RequireEditableFields);
        if (FMapProperty* Map = CastField<FMapProperty>(Property))
            return SupportedMapKey(Map->KeyProp) && Supported(Map->ValueProp, Depth + 1, RequireEditableFields);
        if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Struct->Struct == FGameplayTag::StaticStruct() || Struct->Struct == FGameplayTagContainer::StaticStruct()) return true;
            bool HasFields = false;
            for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
            {
                if ((RequireEditableFields && !Editable(*Field))
                    || (!RequireEditableFields && Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
                    || !Supported(*Field, Depth + 1, RequireEditableFields)) return false;
                HasFields = true;
            }
            return HasFields;
        }
        return CastField<FNumericProperty>(Property) || CastField<FEnumProperty>(Property)
            || CastField<FBoolProperty>(Property) || CastField<FStrProperty>(Property)
            || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property)
            || CastField<FObjectProperty>(Property) || CastField<FSoftObjectProperty>(Property);
    }

    UEnum* EnumType(FProperty* Property)
    {
        if (FEnumProperty* Enum = CastField<FEnumProperty>(Property)) return Enum->GetEnum();
        if (FByteProperty* Byte = CastField<FByteProperty>(Property)) return Byte->Enum;
        return nullptr;
    }

    FNumericProperty* NumericType(FProperty* Property)
    {
        if (FEnumProperty* Enum = CastField<FEnumProperty>(Property)) return Enum->GetUnderlyingProperty();
        return CastField<FNumericProperty>(Property);
    }

    bool AllowedEnum(UEnum* Enum, int32 Index)
    {
        if (Index == INDEX_NONE) return false;
        if (Enum == StaticEnum<EPhysicalSurface>())
            return Enum->GetValueByIndex(Index) >= SurfaceType_Default && Enum->GetValueByIndex(Index) < SurfaceType_Max;
        return !Enum->HasMetaData(TEXT("Hidden"), Index) && !Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX"));
    }

    bool IsUnsigned(FProperty* Property)
    {
        return CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property)
            || CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property);
    }

    TSharedPtr<FJsonValue> Encode(FProperty* Property, const void* Address, FString& Error, int32& Budget, int32 Depth = 0)
    {
        if (--Budget < 0 || Depth > MaxDepth)
        {
            Error = TEXT("Value exceeds traversal budget; select a narrower property path");
            return nullptr;
        }
        if (UEnum* Enum = EnumType(Property))
            return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(NumericType(Property)->GetSignedIntPropertyValue(Address)));
        if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
        {
            double Value = Number->IsFloatingPoint() ? Number->GetFloatingPointPropertyValue(Address)
                : IsUnsigned(Number) ? static_cast<double>(Number->GetUnsignedIntPropertyValue(Address))
                : static_cast<double>(Number->GetSignedIntPropertyValue(Address));
            if (!FMath::IsFinite(Value) || (Number->IsInteger() && FMath::Abs(Value) > MaxExactInteger))
            {
                Error = TEXT("Number cannot be represented safely by this JSON contract");
                return nullptr;
            }
            return MakeShared<FJsonValueNumber>(Value);
        }
        if (FBoolProperty* Bool = CastField<FBoolProperty>(Property)) return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Address));
        if (FStrProperty* String = CastField<FStrProperty>(Property)) return MakeShared<FJsonValueString>(String->GetPropertyValue(Address));
        if (FNameProperty* Name = CastField<FNameProperty>(Property)) return MakeShared<FJsonValueString>(Name->GetPropertyValue(Address).ToString());
        if (FTextProperty* Text = CastField<FTextProperty>(Property)) return MakeShared<FJsonValueString>(Text->GetPropertyValue(Address).ToString());
        if (FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
        {
            const FSoftObjectPtr Value = Soft->GetPropertyValue(Address);
            if (Value.IsNull()) return MakeShared<FJsonValueNull>();
            return MakeShared<FJsonValueString>(Value.ToSoftObjectPath().ToString());
        }
        if (FObjectProperty* Object = CastField<FObjectProperty>(Property))
        {
            UObject* Value = Object->GetObjectPropertyValue(Address);
            if (!Value) return MakeShared<FJsonValueNull>();
            return MakeShared<FJsonValueString>(Value->GetPathName());
        }
        if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper Helper(Map, Address);
            if (Helper.Num() > MaxElements) { Error = TEXT("Map exceeds 1024 entries"); return nullptr; }
            TArray<TSharedPtr<FJsonValue>> Entries;
            for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
            {
                if (!Helper.IsValidIndex(Index)) continue;
                auto Key = Encode(Map->KeyProp, Helper.GetKeyPtr(Index), Error, Budget, Depth + 1);
                auto Value = Encode(Map->ValueProp, Helper.GetValuePtr(Index), Error, Budget, Depth + 1);
                if (!Key || !Value) return nullptr;
                auto Entry = MakeShared<FJsonObject>();
                Entry->SetField(TEXT("key"), Key);
                Entry->SetField(TEXT("value"), Value);
                Entries.Add(MakeShared<FJsonValueObject>(Entry));
            }
            return MakeShared<FJsonValueArray>(Entries);
        }
        if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Helper(Array, Address);
            if (Helper.Num() > MaxElements) { Error = TEXT("Array exceeds 1024 elements; select an element path"); return nullptr; }
            TArray<TSharedPtr<FJsonValue>> Values;
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
            {
                TSharedPtr<FJsonValue> Value = Encode(Array->Inner, Helper.GetRawPtr(Index), Error, Budget, Depth + 1);
                if (!Value) return nullptr;
                Values.Add(Value);
            }
            return MakeShared<FJsonValueArray>(Values);
        }
        if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Struct->Struct == FGameplayTag::StaticStruct())
                return MakeShared<FJsonValueString>(static_cast<const FGameplayTag*>(Address)->ToString());
            if (Struct->Struct == FGameplayTagContainer::StaticStruct())
            {
                TArray<TSharedPtr<FJsonValue>> Tags;
                for (const FGameplayTag& Tag : *static_cast<const FGameplayTagContainer*>(Address)) Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
                return MakeShared<FJsonValueArray>(Tags);
            }
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
            {
                TSharedPtr<FJsonValue> Value = Encode(*Field, Field->ContainerPtrToValuePtr<void>(Address), Error, Budget, Depth + 1);
                if (!Value) return nullptr;
                Result->SetField(Field->GetName(), Value);
            }
            return MakeShared<FJsonValueObject>(Result);
        }
        Error = TEXT("Unsupported value type");
        return nullptr;
    }

    bool Decode(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, FString& Error, int32& Budget, int32 Depth = 0)
    {
        if (!Value || --Budget < 0 || Depth > MaxDepth) { Error = TEXT("Value exceeds traversal budget"); return false; }
        if (UEnum* Enum = EnumType(Property))
        {
            if (Value->Type != EJson::String) { Error = TEXT("Enum requires a declared name"); return false; }
            const int64 EnumValue = Enum->GetValueByNameString(Value->AsString());
            const int32 EnumIndex = Enum->GetIndexByValue(EnumValue);
            if (!AllowedEnum(Enum, EnumIndex))
            { Error = TEXT("Unknown or hidden enum value"); return false; }
            NumericType(Property)->SetIntPropertyValue(Address, static_cast<uint64>(EnumValue));
            return true;
        }
        if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
        {
            if (Value->Type != EJson::Number) { Error = TEXT("Expected JSON number"); return false; }
            const double Input = Value->AsNumber();
            if (!FMath::IsFinite(Input)) { Error = TEXT("Number must be finite"); return false; }
            for (const TCHAR* Bound : {TEXT("ClampMin"), TEXT("ClampMax")})
            {
                if (Property->HasMetaData(Bound))
                {
                    double Limit;
                    if (!LexTryParseString(Limit, *Property->GetMetaData(Bound))) { Error = TEXT("Unsupported numeric clamp metadata"); return false; }
                    if ((FCString::Strcmp(Bound, TEXT("ClampMin")) == 0 && Input < Limit)
                        || (FCString::Strcmp(Bound, TEXT("ClampMax")) == 0 && Input > Limit))
                    { Error = TEXT("Number violates ClampMin/ClampMax"); return false; }
                }
            }
            if (Number->IsInteger())
            {
                const int32 Bits = Number->GetSize() * 8;
                const double Maximum = Bits >= 54 ? MaxExactInteger : FMath::Pow(2.0, IsUnsigned(Number) ? Bits : Bits - 1) - 1;
                const double Minimum = IsUnsigned(Number) ? 0 : Bits >= 54 ? -MaxExactInteger : -FMath::Pow(2.0, Bits - 1);
                if (Input < Minimum || Input > Maximum || FMath::FloorToDouble(Input) != Input)
                { Error = TEXT("Integer out of range or not integral"); return false; }
                Number->SetIntPropertyValue(Address, static_cast<uint64>(static_cast<int64>(Input)));
            }
            else
            {
                if (Number->GetSize() == sizeof(float) && (!FMath::IsFinite(static_cast<float>(Input))
                    || (Input != 0 && static_cast<float>(Input) == 0)))
                { Error = TEXT("Float overflow/underflow"); return false; }
                Number->SetFloatingPointPropertyValue(Address, Input);
            }
            return true;
        }
        if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        {
            if (Value->Type != EJson::Boolean) { Error = TEXT("Expected boolean"); return false; }
            Bool->SetPropertyValue(Address, Value->AsBool()); return true;
        }
        if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
        {
            if (Value->Type != EJson::String || Value->AsString().Len() > 16384) { Error = TEXT("Expected string of at most 16384 characters"); return false; }
            if (FStrProperty* String = CastField<FStrProperty>(Property)) String->SetPropertyValue(Address, Value->AsString());
            if (FNameProperty* Name = CastField<FNameProperty>(Property))
            {
                if (Value->AsString().Len() >= NAME_SIZE) { Error = TEXT("Name too long"); return false; }
                Name->SetPropertyValue(Address, FName(*Value->AsString()));
            }
            if (FTextProperty* Text = CastField<FTextProperty>(Property)) Text->SetPropertyValue(Address, FText::FromString(Value->AsString()));
            return true;
        }
        FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property);
        FObjectProperty* Object = CastField<FObjectProperty>(Property);
        if (Soft || Object)
        {
            UObject* Resolved = nullptr;
            if (Value->Type != EJson::Null)
            {
                if (Value->Type != EJson::String || !Value->AsString().StartsWith(TEXT("/")) || Value->AsString().Contains(TEXT(":")))
                { Error = TEXT("Reference requires a full object/class path or null"); return false; }
                Resolved = LoadObject<UObject>(nullptr, *Value->AsString());
                if (!Resolved) { Error = TEXT("Reference does not resolve; soft references must exist in v1"); return false; }
                UClass* RequiredClass = nullptr;
                if (FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property)) RequiredClass = SoftClass->MetaClass;
                else if (FClassProperty* HardClass = CastField<FClassProperty>(Property)) RequiredClass = HardClass->MetaClass;
                if (RequiredClass)
                {
                    UClass* ResolvedClass = Cast<UClass>(Resolved);
                    if (!ResolvedClass || !ResolvedClass->IsChildOf(RequiredClass))
                    { Error = TEXT("Class reference is incompatible; use a native class or Blueprint generated _C path"); return false; }
                }
                else if (!Resolved->IsA(Soft ? Soft->PropertyClass : Object->PropertyClass) || !Resolved->IsAsset())
                { Error = TEXT("Reference must be a compatible top-level asset"); return false; }
                if (Property->HasMetaData(TEXT("AllowedClasses")) || Property->HasMetaData(TEXT("DisallowedClasses"))
                    || Property->HasMetaData(TEXT("MustImplement")) || Property->HasMetaData(TEXT("ExactClass")))
                { Error = TEXT("Additional reference constraints require a specialized editor"); return false; }
            }
            else if (Property->HasAnyPropertyFlags(CPF_NoClear)) { Error = TEXT("This reference cannot be cleared"); return false; }
            if (Soft) Soft->SetPropertyValue(Address, Resolved ? FSoftObjectPtr(FSoftObjectPath(Resolved)) : FSoftObjectPtr());
            else Object->SetObjectPropertyValue(Address, Resolved);
            return true;
        }
        if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            if (Value->Type != EJson::Array || Value->AsArray().Num() > MaxElements)
            { Error = TEXT("Map requires an array of at most 1024 {key,value} entries"); return false; }
            FScriptMapHelper Helper(Map, Address);
            Helper.EmptyValues();
            for (const auto& Entry : Value->AsArray())
            {
                if (Entry->Type != EJson::Object || Entry->AsObject()->Values.Num() != 2
                    || !Entry->AsObject()->HasField(TEXT("key")) || !Entry->AsObject()->HasField(TEXT("value")))
                { Error = TEXT("Each map entry must contain exactly key and value"); return false; }
                const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
                if (!Decode(Map->KeyProp, Helper.GetKeyPtr(Index), Entry->AsObject()->Values[TEXT("key")], Error, Budget, Depth + 1)
                    || !Decode(Map->ValueProp, Helper.GetValuePtr(Index), Entry->AsObject()->Values[TEXT("value")], Error, Budget, Depth + 1)) return false;
                for (int32 Previous = 0; Previous < Index; ++Previous)
                    if (Helper.IsValidIndex(Previous) && Map->KeyProp->Identical(Helper.GetKeyPtr(Previous), Helper.GetKeyPtr(Index)))
                    { Error = TEXT("Duplicate map key after type conversion"); return false; }
            }
            Helper.Rehash();
            return true;
        }
        if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            if (Value->Type != EJson::Array || Value->AsArray().Num() > MaxElements)
            { Error = TEXT("Expected array with at most 1024 elements"); return false; }
            FScriptArrayHelper Helper(Array, Address);
            if (Property->HasAnyPropertyFlags(CPF_EditFixedSize) && Helper.Num() != Value->AsArray().Num())
            { Error = TEXT("Array has fixed size"); return false; }
            Helper.Resize(Value->AsArray().Num());
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
                if (!Decode(Array->Inner, Helper.GetRawPtr(Index), Value->AsArray()[Index], Error, Budget, Depth + 1)) return false;
            return true;
        }
        if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Struct->Struct == FGameplayTag::StaticStruct() || Struct->Struct == FGameplayTagContainer::StaticStruct())
            {
                const bool Container = Struct->Struct == FGameplayTagContainer::StaticStruct();
                if ((Container && Value->Type != EJson::Array) || (!Container && Value->Type != EJson::String))
                { Error = TEXT("GameplayTag requires a name; GameplayTagContainer requires an array of names"); return false; }
                TArray<TSharedPtr<FJsonValue>> Inputs = Container ? Value->AsArray() : TArray<TSharedPtr<FJsonValue>>{Value};
                if (Inputs.Num() > MaxElements) { Error = TEXT("Too many tags"); return false; }
                FGameplayTagContainer Tags;
                FGameplayTag Single;
                for (const TSharedPtr<FJsonValue>& Input : Inputs)
                {
                    if (Input->Type != EJson::String || Input->AsString().Len() >= NAME_SIZE) { Error = TEXT("Invalid tag name"); return false; }
                    if (!Container && Input->AsString().IsEmpty()) continue;
                    Single = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Input->AsString()), false);
                    if (!Single.IsValid()) { Error = TEXT("GameplayTag is not registered"); return false; }
                    Tags.AddTag(Single);
                }
                if (Container) *static_cast<FGameplayTagContainer*>(Address) = Tags;
                else *static_cast<FGameplayTag*>(Address) = Single;
                return true;
            }
            if (Value->Type != EJson::Object) { Error = TEXT("Struct requires an object with all declared fields"); return false; }
            int32 Fields = 0;
            for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
            {
                ++Fields;
                const TSharedPtr<FJsonValue>* Member = Value->AsObject()->Values.Find(Field->GetName());
                if (!Member) { Error = TEXT("Whole-struct replacement requires all fields; use a nested path for partial edits"); return false; }
                if (!Decode(*Field, Field->ContainerPtrToValuePtr<void>(Address), *Member, Error, Budget, Depth + 1)) return false;
            }
            if (Fields != Value->AsObject()->Values.Num()) { Error = TEXT("Unknown struct field"); return false; }
            return true;
        }
        Error = TEXT("Unsupported property type");
        return false;
    }

    bool ResolveSteps(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Steps, FResolved& Result, FString& Error,
        void* RootStorage = nullptr, bool AllowStructArray = false, bool BlueprintTarget = false, int32* FailedStep = nullptr,
        bool InspectContainer = false)
    {
        if (Steps.IsEmpty() || Steps.Num() > 16) { Error = TEXT("Require 1..16 explicit field/index/key steps"); return false; }
        UStruct* Owner = Asset->GetClass();
        void* Container = Asset;
        FProperty* Property = nullptr;
        void* Address = nullptr;
        for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
        {
            if (FailedStep) *FailedStep = StepIndex;
            const auto& Step = Steps[StepIndex];
            if (!Step.IsValid()) { Error = TEXT("Null path step"); return false; }
            if (Step->Type == EJson::String)
            {
                const FString Name = Step->AsString();
                if (!Owner) { Error = TEXT("Expected an explicit array {index:...} or Map {key:...} selector, not a field name"); return false; }
                if (Name.IsEmpty() || Name.Len() >= NAME_SIZE) { Error = TEXT("Invalid exact field name"); return false; }
                Property = FindFProperty<FProperty>(Owner, *Name);
                if (!Property || Property->GetName() != Name || !Editable(Property) || Property->ArrayDim != 1)
                { Error = TEXT("Unknown, noneditable or instanced property"); return false; }
                if (Property->HasMetaData(TEXT("EditCondition"))) Result.Conditions.Add({Property, Owner, Container});
                if (StepIndex == 0)
                {
                    if (!BlueprintTarget && !AllowedTargetProperty(Asset, Property)) { Error = TEXT("Property is not in the asset-type allowlist"); return false; }
                    if (!Asset->CanEditChange(Property)) { Error = TEXT("Asset disallows editing this property"); return false; }
                    Result.Root = Property;
                }
                Address = StepIndex == 0 && RootStorage ? RootStorage : Property->ContainerPtrToValuePtr<void>(Container);
                Result.Chain.Add(Property);
                Result.CanonicalPath.Add(MakeShared<FJsonValueString>(Name));
            }
            else if (Step->Type == EJson::Object && Step->AsObject()->Values.Num() == 1 && Property)
            {
                const auto Selector = Step->AsObject();
                if (Selector->HasField(TEXT("index")))
                {
                    auto* Array = CastField<FArrayProperty>(Property); double Index;
                    if (!Array || !Selector->TryGetNumberField(TEXT("index"), Index) || !FMath::IsFinite(Index) || Index < 0 || Index > MAX_int32 || Index != FMath::FloorToDouble(Index))
                    { Error = TEXT("Array selector requires {index:nonnegative_integer}; Map keys use {key:typed_value}"); return false; }
                    FScriptArrayHelper Helper(Array, Address);
                    if (Helper.Num() > MaxElements || !Helper.IsValidIndex(static_cast<int32>(Index))) { Error = TEXT("Array index out of range or array exceeds 1024 elements"); return false; }
                    Address = Helper.GetRawPtr(static_cast<int32>(Index)); Property = Array->Inner;
                    auto Canonical = MakeShared<FJsonObject>(); Canonical->SetNumberField(TEXT("index"), Index);
                    Result.CanonicalPath.Add(MakeShared<FJsonValueObject>(Canonical));
                }
                else if (Selector->HasField(TEXT("key")))
                {
                    auto* Map = CastField<FMapProperty>(Property);
                    if (!Map || !SupportedMapKey(Map->KeyProp)) { Error = TEXT("Map selector requires an integer/enum/name/string hashable key; no array-index fallback"); return false; }
                    FValueStorage Key(Map->KeyProp, nullptr); int32 Budget = MaxValues;
                    if (!Decode(Map->KeyProp, Key.Data, Selector->Values[TEXT("key")], Error, Budget)) { Error = TEXT("Map key type: ") + Error; return false; }
                    FScriptMapHelper Helper(Map, Address);
                    if (Helper.Num() > MaxElements) { Error = TEXT("Map exceeds 1024 entries"); return false; }
                    const int32 Index = Helper.FindMapIndexWithKey(Key.Data);
                    if (Index == INDEX_NONE) { Error = TEXT("Map key does not exist; implicit insertion is forbidden"); return false; }
                    auto Canonical = MakeShared<FJsonObject>(); Canonical->SetField(TEXT("key"), Encode(Map->KeyProp, Helper.GetKeyPtr(Index), Error, Budget));
                    if (!Error.IsEmpty()) return false;
                    Result.CanonicalPath.Add(MakeShared<FJsonValueObject>(Canonical));
                    Address = Helper.GetValuePtr(Index); Property = Map->ValueProp;
                }
                else { Error = TEXT("Selector requires exactly index or key"); return false; }
            }
            else { Error = TEXT("A path starts with an exact field string; selectors contain exactly index or key"); return false; }
            if (StepIndex == Steps.Num() - 1)
            {
                auto* Array = CastField<FArrayProperty>(Property);
                const bool ConstructibleArray = AllowStructArray && Array && CastField<FStructProperty>(Array->Inner)
                    && !Array->HasAnyPropertyFlags(CPF_EditFixedSize) && Supported(Array->Inner, 0, false);
                if (!Supported(Property) && !ConstructibleArray && !(InspectContainer && (Array || CastField<FMapProperty>(Property) || CastField<FStructProperty>(Property))))
                { Error = TEXT("Unsupported type or nested ownership; use a specialized editor"); return false; }
                Result.Leaf = Property;
                Result.Address = Address;
                return true;
            }
            auto* Struct = CastField<FStructProperty>(Property);
            if (CastField<FObjectPropertyBase>(Property))
            { Error = TEXT("Only ordinary structs can be traversed; object references are boundaries"); return false; }
            if (Struct && (Struct->Struct == FGameplayTag::StaticStruct() || Struct->Struct == FGameplayTagContainer::StaticStruct()))
            { Error = TEXT("GameplayTag internals are not traversable"); return false; }
            if (!Struct && !CastField<FArrayProperty>(Property) && !CastField<FMapProperty>(Property))
            { Error = TEXT("Cannot traverse a scalar property"); return false; }
            Owner = Struct ? Struct->Struct : nullptr;
            Container = Address;
        }
        return false;
    }

    bool Resolve(UObject* Asset, const FString& Path, FResolved& Result, FString& Error, void* RootStorage = nullptr,
        bool AllowStructArray = false)
    {
        if (Path.IsEmpty() || Path.Len() > 512) { Error = TEXT("Invalid property path"); return false; }
        TArray<FString> Parts; Path.ParseIntoArray(Parts, TEXT("."), false);
        if (Parts.Num() > MaxDepth) { Error = TEXT("Property path too deep"); return false; }
        TArray<TSharedPtr<FJsonValue>> Steps;
        for (const FString& Part : Parts)
        {
            FString Name = Part; int32 Bracket;
            if (Name.FindChar(TEXT('['), Bracket))
            {
                const FString Index = Name.Mid(Bracket + 1, Name.Len() - Bracket - 2);
                if (!Name.EndsWith(TEXT("]")) || Index.IsEmpty() || Index.Len() > 9 || !Index.IsNumeric()
                    || Index.Contains(TEXT(".")) || Index.Contains(TEXT("-")) || Index.Contains(TEXT("+")) || (Index.Len() > 1 && Index[0] == TEXT('0')))
                { Error = TEXT("Array path must use a zero-based integer index"); return false; }
                Steps.Add(MakeShared<FJsonValueString>(Name.Left(Bracket)));
                auto Selector = MakeShared<FJsonObject>(); Selector->SetNumberField(TEXT("index"), FCString::Atoi(*Index));
                Steps.Add(MakeShared<FJsonValueObject>(Selector));
            }
            else Steps.Add(MakeShared<FJsonValueString>(Name));
        }
        return ResolveSteps(Asset, Steps, Result, Error, RootStorage, AllowStructArray);
    }

    TSharedPtr<FJsonObject> Failure(const FString& Error, const FString& Stage = TEXT("validation"), bool Modified = false)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetBoolField(TEXT("modified"), Modified);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("stage"), Stage);
        Result->SetStringField(TEXT("error"), Error);
        return Result;
    }

    UObject* LoadTarget(const TSharedPtr<FJsonObject>& Params, FString& Error)
    {
        FString Path;
        if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
            || !FPackageName::IsValidLongPackageName(Path) || Path.Contains(TEXT(".")))
        { Error = TEXT("Expected a full /Game/... package path"); return nullptr; }
        UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
        if (!Asset || !(Asset->IsA<UDataAsset>() || Asset->GetClass() == UPhysicalMaterial::StaticClass())
            || !Asset->IsAsset() || Asset->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
            || Asset->GetOutermost()->GetName() != Path)
        { Error = TEXT("Target must be an existing DataAsset or PhysicalMaterial instance, not a class, redirector or other asset type. For Blueprint CDO fields use read_blueprint_property_paths; typed nested Map/Array leaves use the matching property_paths reader/patch contract."); return nullptr; }
        return Asset;
    }

    TSharedPtr<FJsonObject> Schema(FProperty* Property, int32 Depth = 0)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
        if (Depth > MaxDepth) { Result->SetStringField(TEXT("restriction"), TEXT("Schema depth exceeds 8")); return Result; }
        Result->SetBoolField(TEXT("edit_defaults_only"), Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
        Result->SetBoolField(TEXT("nullable"), (CastField<FObjectProperty>(Property) || CastField<FSoftObjectProperty>(Property))
            && !Property->HasAnyPropertyFlags(CPF_NoClear));
        if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
        {
            Result->SetStringField(TEXT("json_type"), Number->IsInteger() ? TEXT("integer") : TEXT("number"));
            Result->SetNumberField(TEXT("bits"), Number->GetSize() * 8);
            Result->SetBoolField(TEXT("unsigned"), IsUnsigned(Number));
        }
        if (UEnum* Enum = EnumType(Property))
        {
            TArray<TSharedPtr<FJsonValue>> Names;
            auto DisplayNames = MakeShared<FJsonObject>();
            for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
                if (AllowedEnum(Enum, Index))
                {
                    Names.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
                    DisplayNames->SetStringField(Enum->GetNameStringByIndex(Index), Enum->GetDisplayNameTextByIndex(Index).ToString());
                }
            Result->SetArrayField(TEXT("enum"), Names);
            Result->SetObjectField(TEXT("enum_display_names"), DisplayNames);
        }
        if (FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property)) Result->SetStringField(TEXT("class_constraint"), SoftClass->MetaClass->GetPathName());
        else if (FClassProperty* HardClass = CastField<FClassProperty>(Property)) Result->SetStringField(TEXT("class_constraint"), HardClass->MetaClass->GetPathName());
        else if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property)) Result->SetStringField(TEXT("object_constraint"), SoftObject->PropertyClass->GetPathName());
        else if (FObjectProperty* HardObject = CastField<FObjectProperty>(Property)) Result->SetStringField(TEXT("object_constraint"), HardObject->PropertyClass->GetPathName());
        for (const TCHAR* Key : {TEXT("ClampMin"), TEXT("ClampMax"), TEXT("EditCondition"), TEXT("AllowedClasses"), TEXT("DisallowedClasses"), TEXT("MustImplement"), TEXT("ExactClass")})
            if (Property->HasMetaData(Key)) Result->SetStringField(Key, Property->GetMetaData(Key));
        if (FArrayProperty* Array = CastField<FArrayProperty>(Property)) Result->SetObjectField(TEXT("items"), Schema(Array->Inner, Depth + 1));
        if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            Result->SetStringField(TEXT("format"), TEXT("map_entries"));
            Result->SetObjectField(TEXT("key"), Schema(Map->KeyProp, Depth + 1));
            Result->SetObjectField(TEXT("value"), Schema(Map->ValueProp, Depth + 1));
        }
        if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Struct->Struct == FGameplayTag::StaticStruct()) Result->SetStringField(TEXT("format"), TEXT("registered GameplayTag name; empty string clears"));
            else if (Struct->Struct == FGameplayTagContainer::StaticStruct()) Result->SetStringField(TEXT("format"), TEXT("array of registered GameplayTag names"));
            else
            {
                TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field) Fields->SetObjectField(Field->GetName(), Schema(*Field, Depth + 1));
                Result->SetObjectField(TEXT("fields"), Fields);
            }
        }
        return Result;
    }

    FString TextJson(const TSharedPtr<FJsonObject>& Value)
    {
        FString Text; FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
    }

    TSharedRef<FJsonObject> TextIdentity(const FText& Value)
    {
        auto Result = MakeShared<FJsonObject>(); FString History;
        FTextStringHelper::WriteToBuffer(History, Value);
        Result->SetStringField(TEXT("history"), History);
        FName TableId; FString Key;
        const bool FromTable = FTextInspector::GetTableIdAndKey(Value, TableId, Key);
        Result->SetStringField(TEXT("kind"), FromTable ? TEXT("string_table") : TEXT("other"));
        if (FromTable) { Result->SetStringField(TEXT("table_id"), TableId.ToString()); Result->SetStringField(TEXT("key"), Key); }
        return Result;
    }

    TSharedPtr<FJsonObject> TextSnapshot(UObject* Asset, const TArray<FString>& Paths, FString& Error)
    {
        auto Result = MakeShared<FJsonObject>(); auto Identities = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("text_reference_contract"), 1); Result->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
        Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Fields;
        for (const auto& Path : Paths)
        {
            FResolved Resolved;
            if (!Resolve(Asset, Path, Resolved, Error)) { Error = Path + TEXT(": ") + Error; return nullptr; }
            auto* Property = CastField<FTextProperty>(Resolved.Leaf);
            if (!Property) { Error = Path + TEXT(": not an FText property"); return nullptr; }
            const auto Value = Property->GetPropertyValue(Resolved.Address); auto Identity = TextIdentity(Value);
            if (Identity->GetStringField(TEXT("history")).Len() > 65536) { Error = Path + TEXT(": text history exceeds bound"); return nullptr; }
            auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("path"), Path); Row->SetObjectField(TEXT("identity"), Identity); Row->SetStringField(TEXT("text"), Value.ToString());
            int32 Budget = MaxValues; FString ConditionError; Row->SetBoolField(TEXT("writable"), ConditionsMet(Resolved, nullptr, nullptr, ConditionError, Budget));
            if (!ConditionError.IsEmpty()) Row->SetStringField(TEXT("restriction"), ConditionError);
            Fields.Add(MakeShared<FJsonValueObject>(Row)); Identities->SetObjectField(Path, Identity);
        }
        auto Baseline = MakeShared<FJsonObject>(); Baseline->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName()); Baseline->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        Baseline->SetObjectField(TEXT("identities"), Identities);
        const FString File = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
        Baseline->SetStringField(TEXT("saved_package_hash"), FPaths::FileExists(File) ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("unsaved_package"));
        const auto Text = TextJson(Baseline); const FTCHARToUTF8 Bytes(*Text); FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
        Result->SetStringField(TEXT("baseline"), Hash.ToString()); Result->SetArrayField(TEXT("fields"), Fields); Result->SetBoolField(TEXT("complete"), true);
        Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
        Result->SetStringField(TEXT("semantics"), TEXT("Selected editable DataAsset FText histories. Baseline excludes culture-dependent display strings. History is read-only evidence, never accepted as ImportText instructions.")); return Result;
    }

    bool TextPaths(const TSharedPtr<FJsonObject>& Params, TArray<FString>& Paths, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Params || !Params->TryGetArrayField(TEXT("property_paths"), Values) || Values->IsEmpty() || Values->Num() > 64) { Error = TEXT("property_paths requires 1..64 exact FText paths"); return false; }
        TSet<FString> Seen;
        for (const auto& Value : *Values)
        {
            FString Path; if (!Value || !Value->TryGetString(Path) || Path.IsEmpty() || Path.Len() > 512 || Seen.Contains(Path)) { Error = TEXT("Invalid/duplicate FText path"); return false; }
            Seen.Add(Path); Paths.Add(Path);
        }
        Paths.Sort(); return true;
    }

    bool TextPackagePath(const FString& Path)
    {
        if (Path.Len() >= NAME_SIZE || !Path.StartsWith(TEXT("/Game/")) || Path.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Path)) return false;
        for (const TCHAR Character : Path) if (Character == 0) return false;
        return true;
    }

    FString TextRootValue(FProperty* Property, const void* Address, UObject* Asset)
    {
        FString Value; Property->ExportTextItem_Direct(Value, Address, nullptr, Asset, PPF_None); return Value;
    }

    void AppendEditableFields(UStruct* Struct, const FString& Prefix, TArray<TSharedPtr<FJsonValue>>& Fields, int32 Depth = 0)
    {
        if (Depth > MaxDepth || Fields.Num() >= 64) return;
        for (TFieldIterator<FProperty> Field(Struct); Field && Fields.Num() < 64; ++Field)
        {
            if (!Editable(*Field)) continue;
            const FString Path = Prefix + Field->GetName();
            if (Supported(*Field))
            {
                auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), Path);
                Entry->SetObjectField(TEXT("schema"), Schema(*Field)); Fields.Add(MakeShared<FJsonValueObject>(Entry));
            }
            else if (const auto* Nested = CastField<FStructProperty>(*Field)) AppendEditableFields(Nested->Struct, Path + TEXT("."), Fields, Depth + 1);
        }
    }

    TSharedPtr<FJsonObject> ArrayAppendSnapshot(UObject* Asset, const FString& Path, FString& Error)
    {
        FResolved Resolved;
        if (!Asset->IsA<UDataAsset>() || !Resolve(Asset, Path, Resolved, Error, nullptr, true)) return nullptr;
        auto* Array = CastField<FArrayProperty>(Resolved.Leaf);
        auto* Element = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
        if (!Element || Array->HasAnyPropertyFlags(CPF_EditFixedSize) || !Supported(Element, 0, false)
            || Element->Struct == FGameplayTag::StaticStruct() || Element->Struct == FGameplayTagContainer::StaticStruct())
        { Error = TEXT("Append requires an editable non-fixed ordinary struct array with supported non-instanced stored fields"); return nullptr; }
        FScriptArrayHelper Current(Array, Resolved.Address);
        if (Current.Num() > MaxElements) { Error = TEXT("Array exceeds initialization limit"); return nullptr; }
        int32 Budget = MaxValues; auto Values = Encode(Array, Resolved.Address, Error, Budget);
        FValueStorage Default(Element, nullptr); auto Defaults = Encode(Element, Default.Data, Error, Budget);
        if (!Values || !Defaults) return nullptr;
        auto Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("contract"), 1); Result->SetStringField(TEXT("operation"), TEXT("append_data_asset_struct_array"));
        Result->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName()); Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        Result->SetStringField(TEXT("array_path"), Path); Result->SetStringField(TEXT("element_type"), Element->Struct->GetPathName());
        Result->SetNumberField(TEXT("count"), Current.Num()); Result->SetNumberField(TEXT("max_count"), MaxElements);
        Result->SetNumberField(TEXT("max_new_items"), FMath::Min(64, MaxElements - Current.Num()));
        Result->SetField(TEXT("current_items"), Values); Result->SetField(TEXT("defaults"), Defaults);
        TArray<TSharedPtr<FJsonValue>> Fields; AppendEditableFields(Element->Struct, TEXT(""), Fields); Result->SetArrayField(TEXT("editable_fields"), Fields);
        FString ConditionError; bool Allowed = true;
        for (const auto& Condition : Resolved.Conditions) if (!ConditionMet(Condition, nullptr, ConditionError)) { Allowed = false; break; }
        Result->SetBoolField(TEXT("can_append"), Allowed && Current.Num() < MaxElements);
        if (!ConditionError.IsEmpty()) Result->SetStringField(TEXT("restriction"), ConditionError);
        auto Baseline = MakeShared<FJsonObject>();
        Baseline->SetStringField(TEXT("asset_path"), Asset->GetPathName()); Baseline->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
        Baseline->SetStringField(TEXT("array_path"), Path); Baseline->SetStringField(TEXT("element_type"), Element->Struct->GetPathName());
        Baseline->SetStringField(TEXT("root_value"), TextRootValue(Resolved.Root, Resolved.Root->ContainerPtrToValuePtr<void>(Asset), Asset));
        Baseline->SetStringField(TEXT("element_defaults"), TextRootValue(Element, Default.Data, Asset));
        Baseline->SetArrayField(TEXT("editable_fields"), Fields);
        const FString File = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
        Baseline->SetStringField(TEXT("saved_package_hash"), FPaths::FileExists(File) ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("unsaved_package"));
        const FString Text = TextJson(Baseline);
        if (Text.Len() > 262144) { Error = TEXT("Array baseline exceeds 256 KiB; initialization refused"); return nullptr; }
        const FTCHARToUTF8 Bytes(*Text); FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
        Result->SetStringField(TEXT("baseline"), Hash.ToString()); return Result;
    }

#if WITH_DEV_AUTOMATION_TESTS
    bool FailTextSaveForTest = false;
    bool FailArrayAppendSaveForTest = false;
    bool FailPropertyPathCompileForTest = false;
    bool FailPropertyPathSaveForTest = false;
#endif
}

namespace
{
    struct FPathTarget
    {
        UObject* Object = nullptr;
        UObject* Asset = nullptr;
        UBlueprint* Blueprint = nullptr;
    };

    bool LoadPathTarget(const TSharedPtr<FJsonObject>& Params, bool BlueprintTarget, FPathTarget& Target, FString& Error)
    {
        if (!Params) { Error = TEXT("Missing arguments"); return false; }
        if (!BlueprintTarget) { Target.Asset = Target.Object = LoadTarget(Params, Error); return Target.Object != nullptr; }
        FString Path;
        if (!Params->TryGetStringField(TEXT("blueprint_path"), Path) || !TextPackagePath(Path)) { Error = TEXT("Require an exact /Game Blueprint package path"); return false; }
        auto* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
        if (!Blueprint || Blueprint->GetClass() != UBlueprint::StaticClass() || Blueprint->GetPackage()->GetName() != Path
            || Blueprint->BlueprintType != BPTYPE_Normal || !Blueprint->GeneratedClass || !Blueprint->ParentClass
            || Blueprint->bBeingCompiled || Blueprint->Status == BS_Error || Blueprint->GeneratedClass->ClassGeneratedBy != Blueprint)
        { Error = TEXT("Require a valid ordinary Blueprint with a current generated class/CDO; use specialized editors for animation/widget/level/function-library Blueprints"); return false; }
        Target.Asset = Target.Blueprint = Blueprint; Target.Object = Blueprint->GeneratedClass->GetDefaultObject();
        return Target.Object != nullptr;
    }

    class FPropertyPathArchive final : public FObjectAndNameAsStringProxyArchive
    {
    public:
        explicit FPropertyPathArchive(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, false) { ArNoDelta = true; }
        virtual bool ShouldSkipProperty(const FProperty* Property) const override
        { return Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property); }
        virtual void Serialize(void* Data, int64 Length) override
        {
            if (Length < 0 || Tell() > 1048576 - Length) { SetError(); return; }
            FObjectAndNameAsStringProxyArchive::Serialize(Data, Length);
        }
    };

    FString PathValueHash(FProperty* Property, const void* Data, FString& Error)
    {
        TArray<uint8> Bytes; FMemoryWriter Memory(Bytes); FPropertyPathArchive Writer(Memory);
        Property->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), const_cast<void*>(Data), nullptr);
        if (Writer.IsError()) { Error = TEXT("Stored property protection exceeds 1MiB or cannot be serialized"); return {}; }
        FSHAHash Hash; FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash); return Hash.ToString();
    }

    TSharedPtr<FJsonObject> PathObjectWitness(UObject* Object, const FStagedValues* Staged, FString& Error)
    {
        auto Result = MakeShared<FJsonObject>(); int32 Count = 0;
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
        {
            if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
            if (++Count > 512) { Error = TEXT("Stored object exceeds 512 protection roots"); return nullptr; }
            const auto* Pending = Staged ? Staged->Find(*Field) : nullptr;
            const void* Data = Pending ? (*Pending)->Data : Field->ContainerPtrToValuePtr<void>(Object);
            Result->SetStringField(Field->GetName(), PathValueHash(*Field, Data, Error));
            if (!Error.IsEmpty()) return nullptr;
        }
        return Result;
    }

    bool BlueprintHasDescendants(const FPathTarget& Target)
    {
        if (!Target.Blueprint) return false;
        TSet<FTopLevelAssetPath> Derived;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetDerivedClassNames(
            {Target.Blueprint->GeneratedClass->GetClassPathName()}, {}, Derived);
        Derived.Remove(Target.Blueprint->GeneratedClass->GetClassPathName());
        return !Derived.IsEmpty();
    }

    bool PathLeafWritable(FProperty* Property)
    {
        if (CastField<FArrayProperty>(Property) || CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property) || CastField<FTextProperty>(Property)) return false;
        if (auto* Struct = CastField<FStructProperty>(Property))
            if (Struct->Struct != TBaseStructure<FVector>::Get() && Struct->Struct != TBaseStructure<FRotator>::Get()
                && Struct->Struct != TBaseStructure<FLinearColor>::Get() && Struct->Struct != TBaseStructure<FColor>::Get()
                && Struct->Struct != FGameplayTag::StaticStruct()) return false;
        return Supported(Property);
    }

    bool PathSelections(const TSharedPtr<FJsonObject>& Params, TArray<TArray<TSharedPtr<FJsonValue>>>& Paths, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        if (!Params || !Params->TryGetArrayField(TEXT("paths"), Rows) || Rows->Num() > 64) { Error = TEXT("paths requires 0..64 explicit step arrays; [] discovers editable roots"); return false; }
        for (const auto& Row : *Rows)
        {
            const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
            if (!Row || !Row->TryGetArray(Steps) || Steps->IsEmpty() || Steps->Num() > 16) { Error = TEXT("Each path requires 1..16 field/index/key steps"); return false; }
            Paths.Add(*Steps);
        }
        return true;
    }

    FString PathToken(const TArray<TSharedPtr<FJsonValue>>& Steps)
    { auto Wrapper = MakeShared<FJsonObject>(); Wrapper->SetArrayField(TEXT("path"), Steps); return TextJson(Wrapper); }

    TSharedPtr<FJsonObject> PropertyPathScope(const FPathTarget& Target, const TArray<TArray<TSharedPtr<FJsonValue>>>& Requested, FString& Error)
    {
        auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("property_paths_contract"), 1);
        Result->SetStringField(Target.Blueprint ? TEXT("blueprint_path") : TEXT("asset_path"), Target.Asset->GetPackage()->GetName());
        Result->SetStringField(TEXT("target_kind"), Target.Blueprint ? TEXT("blueprint_cdo") : TEXT("asset_instance"));
        Result->SetStringField(TEXT("class_path"), Target.Object->GetClass()->GetPathName()); Result->SetStringField(TEXT("object_path"), Target.Object->GetPathName());
        Result->SetStringField(TEXT("outer_path"), Target.Object->GetOuter()->GetPathName());
        if (Target.Blueprint) { Result->SetStringField(TEXT("parent_class"), Target.Blueprint->ParentClass->GetPathName()); Result->SetStringField(TEXT("blueprint_guid"), Target.Blueprint->GetBlueprintGuid().ToString()); }
        const bool HasDescendants = BlueprintHasDescendants(Target);
        Result->SetBoolField(TEXT("has_blueprint_descendants"), HasDescendants);
        auto Witness = PathObjectWitness(Target.Object, nullptr, Error); if (!Witness) return nullptr;
        Result->SetObjectField(TEXT("stored_roots"), Witness);
        auto Related = MakeShared<FJsonObject>();
        if (Target.Blueprint)
        {
            int32 Parents = 0;
            for (UClass* Class = Target.Blueprint->ParentClass; Class; Class = Class->GetSuperClass())
            {
                if (++Parents > 32) { Error = TEXT("Parent hierarchy exceeds 32 classes"); return nullptr; }
                UObject* Defaults = Class->GetDefaultObject(false);
                if (!Defaults || (Cast<UBlueprint>(Class->ClassGeneratedBy) && Class->ClassGeneratedBy->GetPackage()->IsDirty()))
                { Error = TEXT("Parent CDO unavailable or parent Blueprint has unsaved changes"); return nullptr; }
                auto Parent = PathObjectWitness(Defaults, nullptr, Error); if (!Parent) return nullptr;
                Related->SetObjectField(Class->GetPathName(), Parent);
            }
        }
        Result->SetObjectField(TEXT("related_defaults"), Related);
        TArray<UObject*> Children; GetObjectsWithOuter(Target.Object, Children, true); auto Owned = MakeShared<FJsonObject>();
        if (Children.Num() > 256) { Error = TEXT("Owned object protection exceeds 256 objects"); return nullptr; }
        for (auto* Child : Children)
        {
            if (Child->HasAnyFlags(RF_Transient)) continue;
            auto State = PathObjectWitness(Child, nullptr, Error); if (!State) return nullptr;
            Owned->SetObjectField(Child->GetPathName(), State);
        }
        Result->SetObjectField(TEXT("owned_objects"), Owned);
        const FString File = FPackageName::LongPackageNameToFilename(Target.Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
        Result->SetStringField(TEXT("saved_file_hash"), FPaths::FileExists(File) ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("unsaved_package"));
        auto Paths = Requested;
        if (Paths.IsEmpty())
            for (TFieldIterator<FProperty> Field(Target.Object->GetClass()); Field && Paths.Num() < 64; ++Field)
                if (Editable(*Field)) Paths.Add({MakeShared<FJsonValueString>(Field->GetName())});
        TArray<TSharedPtr<FJsonValue>> Fields; bool Complete = true; TSet<FString> Seen;
        for (const auto& Steps : Paths)
        {
            auto Row = MakeShared<FJsonObject>(); Row->SetArrayField(TEXT("path"), Steps); FResolved Resolved; int32 FailedStep = 0; FString Reason;
            if (!ResolveSteps(Target.Object, Steps, Resolved, Reason, nullptr, false, Target.Blueprint != nullptr, &FailedStep, true))
            {
                Row->SetBoolField(TEXT("writable"), false); Row->SetStringField(TEXT("reason"), Reason); Row->SetNumberField(TEXT("failed_step"), FailedStep);
                Row->SetStringField(TEXT("code"), TEXT("path_not_supported")); Complete = false;
            }
            else
            {
                const FString Token = PathToken(Resolved.CanonicalPath);
                if (Seen.Contains(Token)) { Error = TEXT("Duplicate paths after native key/name conversion"); return nullptr; }
                Seen.Add(Token); Row->SetArrayField(TEXT("path"), Resolved.CanonicalPath);
                Row->SetObjectField(TEXT("schema"), Schema(Resolved.Leaf)); int32 Budget = MaxValues;
                Row->SetBoolField(TEXT("traversable"), CastField<FStructProperty>(Resolved.Leaf) || CastField<FArrayProperty>(Resolved.Leaf) || CastField<FMapProperty>(Resolved.Leaf));
                const bool Writable = !HasDescendants && PathLeafWritable(Resolved.Leaf) && ConditionsMet(Resolved, nullptr, nullptr, Reason, Budget);
                Row->SetBoolField(TEXT("writable"), Writable);
                if (HasDescendants) Reason = TEXT("Target has derived Blueprint classes; default propagation requires a separate authorized hierarchy edit");
                if (!Writable) Row->SetStringField(TEXT("reason"), Reason.IsEmpty() ? TEXT("Select an editable scalar/native-vector leaf; container replacement, arbitrary structs and FText history require specialized operations") : Reason);
                if (Supported(Resolved.Leaf, 0, false))
                {
                    Budget = MaxValues; auto Value = Encode(Resolved.Leaf, Resolved.Address, Reason, Budget);
                    if (Value) Row->SetField(TEXT("value"), Value); else { Row->SetStringField(TEXT("read_restriction"), Reason); Complete = false; }
                }
                if (auto* Map = CastField<FMapProperty>(Resolved.Leaf))
                {
                    FScriptMapHelper Helper(Map, Resolved.Address); TArray<TSharedPtr<FJsonValue>> Keys;
                    if (Helper.Num() > MaxElements) { Complete = false; Row->SetStringField(TEXT("read_restriction"), TEXT("Map exceeds 1024 entries")); }
                    else if (SupportedMapKey(Map->KeyProp))
                        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
                            if (Helper.IsValidIndex(Index)) { Budget = MaxValues; auto Key = Encode(Map->KeyProp, Helper.GetKeyPtr(Index), Reason, Budget); if (Key) Keys.Add(Key); else Complete = false; }
                    Row->SetArrayField(TEXT("existing_keys"), Keys);
                }
                if (auto* Array = CastField<FArrayProperty>(Resolved.Leaf)) Row->SetNumberField(TEXT("count"), FScriptArrayHelper(Array, Resolved.Address).Num());
            }
            Fields.Add(MakeShared<FJsonValueObject>(Row));
        }
        Result->SetArrayField(TEXT("properties"), Fields); Result->SetBoolField(TEXT("complete"), Complete);
        const FString Text = TextJson(Result);
        if (Text.Len() > 1048576) { Error = TEXT("Property path scope exceeds 1MiB"); return nullptr; }
        if (Complete) { const FTCHARToUTF8 Bytes(*Text); FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash); Result->SetStringField(TEXT("baseline"), Hash.ToString()); }
        else Result->SetField(TEXT("baseline"), MakeShared<FJsonValueNull>());
        Result->SetBoolField(TEXT("package_dirty"), Target.Asset->GetPackage()->IsDirty());
        Result->SetStringField(TEXT("semantics"), TEXT("Explicit reflected field strings, existing {index} arrays and typed {key} Maps. No insertion/deletion/object traversal/ImportText. Witnesses are read-only; legacy contracts unchanged."));
        return Result;
    }
}

TSharedPtr<FJsonObject> ReadPaths(const TSharedPtr<FJsonObject>& Params, bool BlueprintTarget)
{
    FString Error; FPathTarget Target; TArray<TArray<TSharedPtr<FJsonValue>>> Paths;
    if (!Params) return Failure(TEXT("Missing arguments"));
    for (const auto& Pair : Params->Values)
        if (Pair.Key != (BlueprintTarget ? TEXT("blueprint_path") : TEXT("asset_path")) && Pair.Key != TEXT("paths")) return Failure(TEXT("Unknown reader argument: ") + Pair.Key);
    if (!PathSelections(Params, Paths, Error) || !LoadPathTarget(Params, BlueprintTarget, Target, Error)) return Failure(Error);
    auto Result = PropertyPathScope(Target, Paths, Error); if (!Result) return Failure(Error);
    Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
}

TSharedPtr<FJsonObject> PatchPaths(const TSharedPtr<FJsonObject>& Params, bool BlueprintTarget)
{
    auto Reject = [](const FString& Error, const FString& Stage = TEXT("preflight")) { return Failure(Error, Stage); };
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask
        || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid()))
        return Reject(TEXT("Require an idle Editor outside PIE/simulation/save/GC/compilation/modal/slow tasks"));
    if (!Params || TextJson(Params).Len() > 65536) return Reject(TEXT("Missing/oversized request; limit 64Ki characters"));
    const TSet<FString> Allowed{BlueprintTarget ? TEXT("blueprint_path") : TEXT("asset_path"), TEXT("expected_baseline"), TEXT("changes"), TEXT("dry_run"), TEXT("save")};
    for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) return Reject(TEXT("Unknown patch argument: ") + Pair.Key);
    bool DryRun = true, Save = false;
    if ((Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))
        || (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save))) return Reject(TEXT("dry_run/save must be booleans"));
    FString Error, Baseline; FPathTarget Target;
    if (!LoadPathTarget(Params, BlueprintTarget, Target, Error)) return Reject(Error);
    if (Target.Asset->GetPackage()->IsDirty()) return Reject(TEXT("Target has unsaved author edits; never overwrite or save user drafts implicitly"));
    if (BlueprintHasDescendants(Target)) return Reject(TEXT("Target has derived Blueprint classes; indirect default propagation is not authorized"));
    const TArray<TSharedPtr<FJsonValue>>* Changes = nullptr;
    if (!Params->TryGetArrayField(TEXT("changes"), Changes) || Changes->IsEmpty() || Changes->Num() > 64) return Reject(TEXT("Require 1..64 leaf patches"));
    TArray<TArray<TSharedPtr<FJsonValue>>> Paths; TArray<FResolved> Original;
    for (const auto& Value : *Changes)
    {
        const TSharedPtr<FJsonObject>* Change = nullptr; const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
        if (!Value || !Value->TryGetObject(Change) || (*Change)->Values.Num() != 2 || !(*Change)->TryGetArrayField(TEXT("path"), Steps) || !(*Change)->HasField(TEXT("value")))
            return Reject(TEXT("Each patch requires exactly path:[steps] and value"));
        FResolved Resolved;
        if (!ResolveSteps(Target.Object, *Steps, Resolved, Error, nullptr, false, BlueprintTarget) || !PathLeafWritable(Resolved.Leaf))
            return Reject(Error.IsEmpty() ? TEXT("Select a supported editable leaf; container/ordinary-struct/FText replacement is not offered") : Error);
        for (const auto& Previous : Original)
        {
            bool Prefix = true;
            for (int32 Index = 0; Index < FMath::Min(Previous.CanonicalPath.Num(), Resolved.CanonicalPath.Num()); ++Index)
                Prefix &= PathToken({Previous.CanonicalPath[Index]}) == PathToken({Resolved.CanonicalPath[Index]});
            if (Prefix || (Previous.Root == Resolved.Root && Previous.Address == Resolved.Address)) return Reject(TEXT("Duplicate/overlapping canonical paths are forbidden"));
        }
        Paths.Add(Resolved.CanonicalPath); Original.Add(Resolved);
    }
    auto Before = PropertyPathScope(Target, Paths, Error);
    if (!Before || !Before->GetBoolField(TEXT("complete"))) return Reject(Error.IsEmpty() ? TEXT("Incomplete property protection scope") : Error);
    if (!Params->TryGetStringField(TEXT("expected_baseline"), Baseline) || Baseline.Len() != 40 || Baseline != Before->GetStringField(TEXT("baseline")))
        return Reject(TEXT("Missing/stale exact-selection baseline; read and review these same paths again"), TEXT("conflict"));
    FStagedValues Staged; int32 Budget = MaxValues;
    for (int32 Index = 0; Index < Original.Num(); ++Index)
    {
        const auto& Resolved = Original[Index];
        if (!Staged.Contains(Resolved.Root)) Staged.Add(Resolved.Root, MakeShared<FValueStorage>(Resolved.Root, Resolved.Root->ContainerPtrToValuePtr<void>(Target.Object)));
        FResolved Pending;
        if (!ResolveSteps(Target.Object, Paths[Index], Pending, Error, Staged[Resolved.Root]->Data, false, BlueprintTarget)
            || !Decode(Pending.Leaf, Pending.Address, (*Changes)[Index]->AsObject()->Values[TEXT("value")], Error, Budget)) return Reject(Error);
        if (auto* Object = CastField<FObjectPropertyBase>(Pending.Leaf))
        {
            if (UObject* Reference = Object->GetObjectPropertyValue(Pending.Address))
                if (Reference->GetPackage()->IsDirty()) return Reject(TEXT("Requested reference has unsaved changes; do not save it implicitly"));
        }
    }
    Budget = MaxValues;
    for (int32 Index = 0; Index < Paths.Num(); ++Index)
    {
        FResolved Pending;
        if (!ResolveSteps(Target.Object, Paths[Index], Pending, Error, Staged[Original[Index].Root]->Data, false, BlueprintTarget)
            || !ConditionsMet(Pending, Original[Index].Address, &Staged, Error, Budget)) return Reject(Error, TEXT("edit_condition"));
    }
    auto ExpectedRoots = PathObjectWitness(Target.Object, &Staged, Error); if (!ExpectedRoots) return Reject(Error);
    auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("property_paths_contract"), 1); Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("compiled"), false); Result->SetBoolField(TEXT("saved"), false);
    Result->SetBoolField(TEXT("package_dirty"), false); Result->SetObjectField(TEXT("before"), Before); Result->SetObjectField(TEXT("after"), Before);
    TArray<TSharedPtr<FJsonValue>> Proposed;
    for (int32 Index = 0; Index < Paths.Num(); ++Index)
    {
        FResolved Pending; ResolveSteps(Target.Object, Paths[Index], Pending, Error, Staged[Original[Index].Root]->Data, false, BlueprintTarget);
        auto Row = MakeShared<FJsonObject>(); Row->SetArrayField(TEXT("path"), Paths[Index]); Budget = MaxValues;
        Row->SetField(TEXT("value"), Encode(Pending.Leaf, Pending.Address, Error, Budget)); Proposed.Add(MakeShared<FJsonValueObject>(Row));
        if (!Error.IsEmpty()) return Reject(Error);
    }
    Result->SetArrayField(TEXT("proposed"), Proposed);
    const bool Changed = TextJson(ExpectedRoots) != TextJson(Before->GetObjectField(TEXT("stored_roots")));
    Result->SetBoolField(TEXT("would_modify"), Changed);
    if (DryRun || !Changed) return Result;
    auto Current = PropertyPathScope(Target, Paths, Error);
    if (!Current || Current->GetStringField(TEXT("baseline")) != Baseline || Target.Asset->GetPackage()->IsDirty() || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0)
        return Reject(TEXT("Target/reference/compilation changed during preflight; read again"), TEXT("conflict"));
    {
        const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "TypedPropertyPaths", "Patch reflected property leaves"));
        Target.Asset->SetFlags(RF_Transactional); Target.Object->SetFlags(RF_Transactional); Target.Asset->Modify();
        if (Target.Object != Target.Asset) Target.Object->Modify();
        for (const auto& Pair : Staged) Target.Object->PreEditChange(Pair.Key);
        for (const auto& Pair : Staged) Pair.Key->CopyCompleteValue(Pair.Key->ContainerPtrToValuePtr<void>(Target.Object), Pair.Value->Data);
        for (const auto& Resolved : Original)
        {
            FEditPropertyChain Chain; for (auto* Property : Resolved.Chain) Chain.AddTail(Property);
            Chain.SetActiveMemberPropertyNode(Resolved.Root); Chain.SetActivePropertyNode(Resolved.Leaf);
            FPropertyChangedEvent Event(Resolved.Leaf, EPropertyChangeType::ValueSet); Event.SetActiveMemberProperty(Resolved.Root);
            FPropertyChangedChainEvent ChainEvent(Chain, Event); Target.Object->PostEditChangeChainProperty(ChainEvent);
        }
        Target.Asset->MarkPackageDirty();
        if (Target.Blueprint) FBlueprintEditorUtils::MarkBlueprintAsModified(Target.Blueprint);
    }
    Staged.Empty(); Original.Empty(); Result->SetBoolField(TEXT("modified"), true);
    auto ReadAfter = [&]()
    {
        Error.Empty(); FPathTarget Actual;
        if (!LoadPathTarget(Params, BlueprintTarget, Actual, Error)) { Result->SetBoolField(TEXT("package_dirty"), Target.Asset->GetPackage()->IsDirty()); return TSharedPtr<FJsonObject>(); }
        Target = Actual; auto After = PropertyPathScope(Target, Paths, Error);
        if (After) Result->SetObjectField(TEXT("after"), After);
        Result->SetBoolField(TEXT("package_dirty"), Target.Asset->GetPackage()->IsDirty()); return After;
    };
    auto FailAfter = [&](const TCHAR* Stage, const FString& Message)
    { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), Stage); Result->SetStringField(TEXT("error"), Message + TEXT("; actual changes may remain, no rollback/retry")); ReadAfter(); return Result; };
    if (Target.Blueprint)
    {
#if WITH_DEV_AUTOMATION_TESTS
        if (FailPropertyPathCompileForTest) return FailAfter(TEXT("compile"), TEXT("Injected target compile failure"));
#endif
        FKismetEditorUtilities::CompileBlueprint(Target.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
        Result->SetBoolField(TEXT("compiled"), true);
        if (Target.Blueprint->Status == BS_Error) return FailAfter(TEXT("compile"), TEXT("Target Blueprint compilation failed"));
    }
    auto Verify = [&]()
    {
        auto After = ReadAfter(); if (!After || !After->GetBoolField(TEXT("complete"))) return false;
        bool Preserved = TextJson(After->GetObjectField(TEXT("stored_roots"))) == TextJson(ExpectedRoots);
        for (const auto* Name : {TEXT("related_defaults"), TEXT("owned_objects")}) Preserved &= TextJson(After->GetObjectField(Name)) == TextJson(Before->GetObjectField(Name));
        for (const auto* Name : {TEXT("class_path"), TEXT("object_path"), TEXT("outer_path")}) Preserved &= After->GetStringField(Name) == Before->GetStringField(Name);
        if (BlueprintTarget) Preserved &= After->GetStringField(TEXT("parent_class")) == Before->GetStringField(TEXT("parent_class")) && After->GetStringField(TEXT("blueprint_guid")) == Before->GetStringField(TEXT("blueprint_guid"));
        Result->SetBoolField(TEXT("existing_data_preserved"), Preserved); return Preserved;
    };
    if (!Verify()) return FailAfter(TEXT("readback"), TEXT("Typed values, identity or protected data differ after edit/compile: ") + Error);
    if (Save)
    {
        bool Saved = false;
#if WITH_DEV_AUTOMATION_TESTS
        if (!FailPropertyPathSaveForTest)
#endif
            Saved = UEditorAssetLibrary::SaveLoadedAsset(Target.Asset, false) && !Target.Asset->GetPackage()->IsDirty();
        Result->SetBoolField(TEXT("saved"), Saved);
        if (!Saved) return FailAfter(TEXT("save"), TEXT("Target-only save failed"));
        if (!Verify()) return FailAfter(TEXT("readback"), TEXT("Protected data differ after save: ") + Error);
    }
    return Result;
}

TSharedPtr<FJsonObject> ReadText(const TSharedPtr<FJsonObject>& Params)
{
    FString Error; TArray<FString> Paths;
    if (!TextPaths(Params, Paths, Error)) return Failure(Error);
    for (const auto& Field : Params->Values) if (Field.Key != TEXT("asset_path") && Field.Key != TEXT("property_paths")) return Failure(TEXT("Unknown text reader argument"));
    FString AssetPath; if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || !TextPackagePath(AssetPath)) return Failure(TEXT("Expected bounded /Game package path"));
    auto* Asset = LoadTarget(Params, Error);
    if (!Asset || !Asset->IsA<UDataAsset>()) return Failure(Error.IsEmpty() ? TEXT("Text references require a DataAsset instance") : Error);
    auto Result = TextSnapshot(Asset, Paths, Error); if (!Result) return Failure(Error);
    Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
void SetTextSaveFailureForTests(bool Fail) { FailTextSaveForTest = Fail; }
void SetArrayAppendSaveFailureForTests(bool Fail) { FailArrayAppendSaveForTest = Fail; }
void SetPropertyPathFailuresForTests(bool Compile, bool Save) { FailPropertyPathCompileForTest = Compile; FailPropertyPathSaveForTest = Save; }
#endif

TSharedPtr<FJsonObject> BindText(const TSharedPtr<FJsonObject>& Params)
{
    auto Reject = [](const FString& Error) { return Failure(Error, TEXT("preflight")); };
    if (!Params) return Reject(TEXT("Missing arguments"));
    for (const auto& Field : Params->Values)
        if (Field.Key != TEXT("asset_path") && Field.Key != TEXT("expected_baseline") && Field.Key != TEXT("bindings") && Field.Key != TEXT("dry_run") && Field.Key != TEXT("save")) return Reject(TEXT("Unknown text binding argument"));
    if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask
        || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0) return Reject(TEXT("Requires idle editor, outside PIE, saving, GC and asset compilation"));
    bool DryRun = true, Save = false;
    if ((Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))
        || (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save))) return Reject(TEXT("dry_run/save must be boolean"));
    FString AssetPath, Expected, Error;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || !TextPackagePath(AssetPath)
        || !Params->TryGetStringField(TEXT("expected_baseline"), Expected) || Expected.Len() != 40) return Reject(TEXT("Requires bounded /Game package path and exact reader baseline"));
    auto* Asset = LoadTarget(Params, Error);
    if (!Asset || !Asset->IsA<UDataAsset>()) return Reject(Error.IsEmpty() ? TEXT("Requires DataAsset instance") : Error);
    if (Asset->GetPackage()->IsDirty()) return Reject(TEXT("Target has unsaved changes; save or discard explicitly first"));
    const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
    if (!Params->TryGetArrayField(TEXT("bindings"), Bindings) || Bindings->IsEmpty() || Bindings->Num() > 64) return Reject(TEXT("bindings requires 1..64 rows"));
    FStagedValues Staged;
    TArray<FString> Paths;
    TMap<FString, TSharedPtr<FJsonObject>> Desired;
    TMap<FProperty*, FString> ProposedRoots;
    TSet<FProperty*> ChangedRoots;
    TMap<UStringTable*, FString> TableHashes;
    for (const auto& Value : *Bindings)
    {
        if (!Value || Value->Type != EJson::Object) return Reject(TEXT("Binding must be object"));
        const auto Row = Value->AsObject();
        for (const auto& Field : Row->Values)
            if (Field.Key != TEXT("path") && Field.Key != TEXT("table_path") && Field.Key != TEXT("key") && Field.Key != TEXT("expected_table_id") && Field.Key != TEXT("expected_namespace")) return Reject(TEXT("Unknown binding field"));
        FString Path, TablePath, Key, TableId, Namespace;
        if (!Row->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty() || Path.Len() > 512 || Paths.Contains(Path)
            || !Row->TryGetStringField(TEXT("table_path"), TablePath) || !TextPackagePath(TablePath)
            || !Row->TryGetStringField(TEXT("key"), Key) || Key.TrimStartAndEnd().IsEmpty() || Key.Len() > 1024
            || !Row->TryGetStringField(TEXT("expected_table_id"), TableId) || TableId.Len() >= NAME_SIZE
            || !Row->TryGetStringField(TEXT("expected_namespace"), Namespace) || Namespace.Len() > 1024) return Reject(TEXT("Requires unique path, canonical table package, existing key, expected_table_id and expected_namespace"));
        for (const TCHAR Character : Key) if (Character == 0 || Character == '\r' || Character == '\n') return Reject(TEXT("Invalid key character"));
        FResolved Original;
        if (!Resolve(Asset, Path, Original, Error) || !CastField<FTextProperty>(Original.Leaf)) return Reject(Path + TEXT(": ") + (Error.IsEmpty() ? TEXT("not FText") : Error));
        if (!UEditorAssetLibrary::DoesAssetExist(TablePath)) return Reject(TEXT("String Table does not exist"));
        auto* Table = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(TablePath));
        if (!Table || !Table->IsAsset() || Table->GetPackage()->GetName() != TablePath || Table->GetPackage()->IsDirty()
            || !FPackageName::DoesPackageExist(TablePath)) return Reject(TEXT("Table must be an existing saved clean UStringTable, not redirector"));
        if (Table->GetStringTableId().ToString() != TableId || Table->GetStringTable()->GetNamespace() != Namespace
            || !Table->GetStringTable()->FindEntry(FTextKey(Key)).IsValid()) return Reject(TEXT("Table ID/namespace mismatch or missing key"));
        const FString TableFile = FPackageName::LongPackageNameToFilename(TablePath, FPackageName::GetAssetPackageExtension());
        TableHashes.Add(Table, LexToString(FMD5Hash::HashFile(*TableFile)));
        if (!Staged.Contains(Original.Root)) Staged.Add(Original.Root, MakeShared<FValueStorage>(Original.Root, Original.Root->ContainerPtrToValuePtr<void>(Asset)));
        FResolved Pending;
        if (!Resolve(Asset, Path, Pending, Error, Staged[Original.Root]->Data)) return Reject(Path + TEXT(": ") + Error);
        const FText Native = FText::FromStringTable(Table->GetStringTableId(), Key);
        const auto Identity = TextIdentity(Native);
        if (Identity->GetStringField(TEXT("kind")) != TEXT("string_table") || Identity->GetStringField(TEXT("table_id")) != TableId || Identity->GetStringField(TEXT("key")) != Key) return Reject(TEXT("Native text identity did not resolve exactly"));
        const auto* Property = CastFieldChecked<FTextProperty>(Pending.Leaf);
        if (TextJson(TextIdentity(Property->GetPropertyValue(Original.Address))) != TextJson(Identity)) ChangedRoots.Add(Original.Root);
        Property->SetPropertyValue(Pending.Address, Native);
        Desired.Add(Path, Identity); Paths.Add(Path);
    }
    Paths.Sort();
    auto Before = TextSnapshot(Asset, Paths, Error);
    if (!Before || Before->GetStringField(TEXT("baseline")) != Expected) return Reject(TEXT("Stale or mismatched selected-path baseline"));
    for (const FString& Path : Paths)
    {
        FResolved Original, Pending; int32 Budget = MaxValues;
        if (!Resolve(Asset, Path, Original, Error) || !Resolve(Asset, Path, Pending, Error, Staged[Original.Root]->Data)
            || !ConditionsMet(Pending, Original.Address, &Staged, Error, Budget)) return Reject(Path + TEXT(": ") + Error);
    }
    for (const auto& Entry : Staged) ProposedRoots.Add(Entry.Key, TextRootValue(Entry.Key, Entry.Value->Data, Asset));
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("text_reference_contract"), 1); Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), !ChangedRoots.IsEmpty());
    Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
    Result->SetObjectField(TEXT("before"), Before); Result->SetObjectField(TEXT("after"), Before);
    auto Proposal = MakeShared<FJsonObject>(); for (const FString& Path : Paths) Proposal->SetObjectField(Path, Desired[Path]); Result->SetObjectField(TEXT("proposed_identities"), Proposal);
    Result->SetStringField(TEXT("stage"), DryRun ? TEXT("preview") : TEXT("no_op"));
    if (DryRun || ChangedRoots.IsEmpty()) return Result;
    {
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "BindDataAssetText", "Bind DataAsset String Table references"));
        Asset->Modify();
        for (auto* Root : ChangedRoots) { Asset->PreEditChange(Root); Root->CopyCompleteValue(Root->ContainerPtrToValuePtr<void>(Asset), Staged[Root]->Data); }
        Asset->MarkPackageDirty();
        for (auto* Root : ChangedRoots) { FPropertyChangedEvent Event(Root, EPropertyChangeType::ValueSet); Asset->PostEditChangeProperty(Event); }
    }
    Result->SetBoolField(TEXT("modified"), true);
    auto FailAfter = [&](const FString& Stage, const FString& Message)
    {
        Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), Stage); Result->SetStringField(TEXT("error"), Message);
        Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty()); return Result;
    };
    auto Verify = [&]()
    {
        auto After = TextSnapshot(Asset, Paths, Error);
        if (!After) { Result->SetField(TEXT("after"), MakeShared<FJsonValueNull>()); return false; }
        Result->SetObjectField(TEXT("after"), After);
        for (const auto& Field : After->GetArrayField(TEXT("fields"))) if (TextJson(Field->AsObject()->GetObjectField(TEXT("identity"))) != TextJson(Desired[Field->AsObject()->GetStringField(TEXT("path"))])) return false;
        for (const auto& Entry : ProposedRoots) if (TextRootValue(Entry.Key, Entry.Key->ContainerPtrToValuePtr<void>(Asset), Asset) != Entry.Value) return false;
        for (const auto& Entry : TableHashes)
        {
            const FString File = FPackageName::LongPackageNameToFilename(Entry.Key->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
            if (Entry.Key->GetPackage()->IsDirty() || LexToString(FMD5Hash::HashFile(*File)) != Entry.Value) return false;
        }
        return true;
    };
    if (!Verify()) return FailAfter(TEXT("readback"), TEXT("Post-edit identity/root/table verification failed; in-memory edits remain, not saved. Do not retry."));
    bool Saved = false;
    if (Save)
    {
        bool AllowSave = true;
#if WITH_DEV_AUTOMATION_TESTS
        AllowSave = !FailTextSaveForTest;
#endif
        Saved = AllowSave && UEditorAssetLibrary::SaveLoadedAsset(Asset, false) && !Asset->GetPackage()->IsDirty() && FPackageName::DoesPackageExist(AssetPath);
        Result->SetBoolField(TEXT("saved"), Saved);
        if (!Saved) return FailAfter(TEXT("save"), TEXT("Save failed; applied in-memory edits remain. No rollback or retry performed."));
        if (!Verify()) return FailAfter(TEXT("readback"), TEXT("Saved but post-save verification failed; inspect after and disk before further writes."));
    }
    Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty()); Result->SetStringField(TEXT("stage"), Saved ? TEXT("saved") : TEXT("applied")); return Result;
}

TSharedPtr<FJsonValue> EncodeValue(FProperty* Property, const void* Address, FString& Error)
{
    if (!Supported(Property)) { Error = TEXT("Unsupported value type"); return nullptr; }
    int32 Budget = MaxValues;
    return Encode(Property, Address, Error, Budget);
}

bool DecodeValue(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, FString& Error)
{
    if (!Supported(Property)) { Error = TEXT("Unsupported value type"); return false; }
    int32 Budget = MaxValues;
    return Decode(Property, Address, Value, Error, Budget);
}

TSharedPtr<FJsonObject> DescribeValue(FProperty* Property)
{
    return Schema(Property);
}

static TSharedPtr<FJsonObject> CreateAllowedAsset(const TSharedPtr<FJsonObject>& Params, bool Physical)
{
    FString Path;
    FString ClassPath;
    bool Save = true;
    if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
        || !FPackageName::IsValidLongPackageName(Path) || Path.Contains(TEXT("."))) return Failure(TEXT("Expected full /Game/... package path"));
    if (Physical) ClassPath = UPhysicalMaterial::StaticClass()->GetPathName();
    else if (!Params->TryGetStringField(TEXT("class_path"), ClassPath)
        || !(ClassPath.StartsWith(TEXT("/Script/")) || ClassPath.StartsWith(TEXT("/Game/")))
        || ClassPath.Contains(TEXT(":"))) return Failure(TEXT("Expected native or Blueprint generated class path"));
    if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be boolean"));
    if (FindPackage(nullptr, *Path) || FPackageName::DoesPackageExist(Path) || UEditorAssetLibrary::DoesAssetExist(Path))
        return Failure(TEXT("Destination exists; asset creation never overwrites"));
    UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
    if (!Class || (Physical ? Class != UPhysicalMaterial::StaticClass() : !Class->IsChildOf(UDataAsset::StaticClass()))
        || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        return Failure(TEXT("Only concrete classes in the requested asset family are allowed"));
    UPackage* Package = CreatePackage(*Path);
    const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "CreateDataAsset", "Create DataAsset"));
    UObject* Asset = NewObject<UObject>(Package, Class, *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional);
    Asset->Modify();
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    const bool Saved = Save && UEditorAssetLibrary::SaveLoadedAsset(Asset, false)
        && !Package->IsDirty() && FPackageName::DoesPackageExist(Path);
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), !Save || Saved);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("modified"), true);
    Result->SetBoolField(TEXT("saved"), Saved);
    Result->SetBoolField(TEXT("package_dirty"), Package->IsDirty());
    Result->SetStringField(TEXT("asset_path"), Path);
    Result->SetStringField(TEXT("class_path"), Class->GetPathName());
    if (Save && !Saved)
    {
        Result->SetStringField(TEXT("stage"), TEXT("save"));
        Result->SetStringField(TEXT("error"), TEXT("Created in memory but save failed; do not retry creation"));
    }
    return Result;
}

TSharedPtr<FJsonObject> CreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
    return CreateAllowedAsset(Params, false);
}

TSharedPtr<FJsonObject> CreatePhysicalMaterial(const TSharedPtr<FJsonObject>& Params)
{
    return CreateAllowedAsset(Params, true);
}

TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UObject* Asset = LoadTarget(Params, Error);
    if (!Asset) return Failure(Error);
    TArray<FString> Paths;
    const TArray<TSharedPtr<FJsonValue>>* Requested;
    const bool Selected = Params->HasField(TEXT("property_paths"));
    if (Selected)
    {
        if (!Params->TryGetArrayField(TEXT("property_paths"), Requested) || Requested->Num() > 64) return Failure(TEXT("property_paths must contain at most 64 paths"));
        for (const TSharedPtr<FJsonValue>& Value : *Requested)
        {
            if (Value->Type != EJson::String) return Failure(TEXT("Property path must be a string"));
            Paths.Add(Value->AsString());
        }
    }
    else
    {
        FString Category;
        Params->TryGetStringField(TEXT("category"), Category);
        for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
            if (Field->HasAnyPropertyFlags(CPF_Edit) && (Category.IsEmpty() || Field->GetMetaData(TEXT("Category")).Contains(Category))) Paths.Add(Field->GetName());
    }
    int32 Budget = MaxValues;
    TArray<TSharedPtr<FJsonValue>> Fields;
    for (const FString& Path : Paths)
    {
        TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
        Field->SetStringField(TEXT("path"), Path);
        FResolved Resolved;
        Error.Reset();
        bool Writable = Resolve(Asset, Path, Resolved, Error);
        if (Writable)
        {
            TSharedPtr<FJsonValue> Value = Encode(Resolved.Leaf, Resolved.Address, Error, Budget);
            Writable = Value.IsValid();
            if (Value) Field->SetField(TEXT("value"), Value);
            Field->SetObjectField(TEXT("schema"), Schema(Resolved.Leaf));
            if (Writable)
            {
                int32 ConditionBudget = MaxValues;
                Writable = ConditionsMet(Resolved, nullptr, nullptr, Error, ConditionBudget);
            }
        }
        Field->SetBoolField(TEXT("writable"), Writable);
        if (!Error.IsEmpty()) Field->SetStringField(TEXT("reason"), Error);
        FString AppendError;
        if (auto Append = ArrayAppendSnapshot(Asset, Path, AppendError)) Field->SetObjectField(TEXT("array_append"), Append);
        Fields.Add(MakeShared<FJsonValueObject>(Field));
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Asset->GetOutermost()->GetName());
    Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
    Result->SetBoolField(TEXT("package_dirty"), Asset->GetOutermost()->IsDirty());
    Result->SetArrayField(TEXT("properties"), Fields);
    Result->SetStringField(TEXT("contract"), TEXT("v2: exact paths; whole struct/array/map replacement; maps use [{key,value}]; existing refs; safe JSON integers; no object traversal, set or instanced data"));
    return Result;
}

TSharedPtr<FJsonObject> AppendStructArray(const TSharedPtr<FJsonObject>& Params)
{
    auto Reject = [](const FString& Error) { return Failure(Error, TEXT("preflight")); };
    if (!Params) return Reject(TEXT("Missing arguments"));
    for (const auto& Pair : Params->Values)
        if (Pair.Key != TEXT("asset_path") && Pair.Key != TEXT("array_path") && Pair.Key != TEXT("expected_baseline")
            && Pair.Key != TEXT("items") && Pair.Key != TEXT("dry_run") && Pair.Key != TEXT("save")) return Reject(TEXT("Unknown array append argument"));
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || GIsSavingPackage
        || IsGarbageCollecting() || GIsSlowTask || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0)
        return Reject(TEXT("Requires idle Editor GameThread outside PIE, simulation, saving, GC and asset compilation"));
    FString AssetPath, Path, Expected, Error;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || !TextPackagePath(AssetPath)
        || !Params->TryGetStringField(TEXT("array_path"), Path) || Path.IsEmpty() || Path.Len() > 512
        || !Params->TryGetStringField(TEXT("expected_baseline"), Expected) || Expected.Len() != 40)
        return Reject(TEXT("Require bounded /Game package path, exact array_path and reader array_append.baseline"));
    bool DryRun = true, Save = false;
    if ((Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))
        || (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save))) return Reject(TEXT("dry_run/save must be boolean"));
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Params->TryGetArrayField(TEXT("items"), Items) || Items->IsEmpty() || Items->Num() > 64)
        return Reject(TEXT("items requires 1..64 objects of relative editable field paths, e.g. [{Cost:75},{Cost:100}]"));
    auto* Asset = LoadTarget(Params, Error);
    if (!Asset || !Asset->IsA<UDataAsset>()) return Reject(Error.IsEmpty() ? TEXT("Append requires a DataAsset instance") : Error);
    if (Asset->GetPackage()->IsDirty()) return Reject(TEXT("Preexisting dirty asset refused, even with save=false; save or discard the approved draft explicitly"));
    auto Before = ArrayAppendSnapshot(Asset, Path, Error);
    if (!Before) return Reject(Error);
    if (Before->GetStringField(TEXT("baseline")) != Expected) return Failure(TEXT("Stale or mismatched array append baseline; read again before appending"), TEXT("conflict"));
    if (!Before->GetBoolField(TEXT("can_append")) || Items->Num() > Before->GetIntegerField(TEXT("max_new_items")))
        return Reject(TEXT("Array cannot currently append or requested item count exceeds the remaining capacity/batch limit"));
    FResolved Original;
    if (!Resolve(Asset, Path, Original, Error, nullptr, true)) return Reject(Error);
    FValueStorage Staged(Original.Root, Original.Root->ContainerPtrToValuePtr<void>(Asset));
    FResolved Pending;
    if (!Resolve(Asset, Path, Pending, Error, Staged.Data, true)) return Reject(Error);
    auto* Array = CastFieldChecked<FArrayProperty>(Pending.Leaf);
    FScriptArrayHelper Storage(Array, Pending.Address);
    const int32 OldCount = Storage.Num(); Storage.Resize(OldCount + Items->Num());
    TArray<FString> ChangedPaths;
    int32 Budget = MaxValues;
    for (int32 Index = 0; Index < Items->Num(); ++Index)
    {
        const auto& Item = (*Items)[Index];
        if (!Item || Item->Type != EJson::Object || Item->AsObject()->Values.Num() > 64) return Reject(TEXT("Each new item requires at most 64 relative field/value pairs"));
        TArray<FString> Fields; Item->AsObject()->Values.GetKeys(Fields); Fields.Sort();
        TArray<FString> Previous;
        for (const auto& Field : Fields)
        {
            if (Field.IsEmpty() || Field.Len() > 256) return Reject(TEXT("Invalid relative new-item field path"));
            for (const auto& Other : Previous)
                if (Field == Other || Field.StartsWith(Other + TEXT(".")) || Field.StartsWith(Other + TEXT("["))
                    || Other.StartsWith(Field + TEXT(".")) || Other.StartsWith(Field + TEXT("["))) return Reject(TEXT("Overlapping new-item field paths"));
            const FString LeafPath = FString::Printf(TEXT("%s[%d].%s"), *Path, OldCount + Index, *Field);
            FResolved Leaf;
            if (!Resolve(Asset, LeafPath, Leaf, Error, Staged.Data) || !Decode(Leaf.Leaf, Leaf.Address, Item->AsObject()->Values[Field], Error, Budget))
                return Reject(LeafPath + TEXT(": ") + Error);
            Previous.Add(Field); ChangedPaths.Add(LeafPath);
        }
    }
    for (const auto& Condition : Pending.Conditions) if (!ConditionMet(Condition, nullptr, Error)) return Reject(Error);
    int32 ConditionBudget = MaxValues;
    for (const auto& LeafPath : ChangedPaths)
    {
        FResolved Leaf;
        if (!Resolve(Asset, LeafPath, Leaf, Error, Staged.Data) || !ConditionsMet(Leaf, nullptr, nullptr, Error, ConditionBudget))
            return Failure(LeafPath + TEXT(": ") + Error, TEXT("edit_condition"));
    }
    Budget = MaxValues;
    const auto Proposal = Encode(Array, Pending.Address, Error, Budget);
    if (!Proposal) return Reject(Error);
    TMap<FProperty*, FString> Witness;
    int32 WitnessSize = 0;
    for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
        const FString Value = TextRootValue(*Field, *Field == Original.Root ? Staged.Data : Field->ContainerPtrToValuePtr<void>(Asset), Asset);
        WitnessSize += Value.Len();
        if (WitnessSize > 1048576) return Reject(TEXT("Asset readback witness exceeds 1 MiB; append refused"));
        Witness.Add(*Field, Value);
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("array_append_contract"), 1); Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), AssetPath); Result->SetStringField(TEXT("array_path"), Path);
    Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), true);
    Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
    Result->SetBoolField(TEXT("package_dirty"), false); Result->SetNumberField(TEXT("old_count"), OldCount);
    Result->SetNumberField(TEXT("new_count"), Storage.Num()); Result->SetObjectField(TEXT("before"), Before);
    Result->SetObjectField(TEXT("after"), Before); Result->SetField(TEXT("proposed_items"), Proposal);
    Result->SetStringField(TEXT("stage"), DryRun ? TEXT("preview") : TEXT("apply"));
    if (DryRun) return Result;
    {
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "AppendDataAssetStructArray", "Append DataAsset struct array items"));
        Asset->Modify(); Asset->PreEditChange(Original.Root);
        Original.Root->CopyCompleteValue(Original.Root->ContainerPtrToValuePtr<void>(Asset), Staged.Data);
        Asset->MarkPackageDirty();
        FPropertyChangedEvent Event(Original.Root, EPropertyChangeType::ArrayAdd); Asset->PostEditChangeProperty(Event);
    }
    Result->SetBoolField(TEXT("modified"), true);
    auto FailAfter = [&](const FString& Stage, const FString& Message)
    {
        Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), Stage);
        Result->SetStringField(TEXT("error"), Message); Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty()); return Result;
    };
    auto Verify = [&]()
    {
        auto After = ArrayAppendSnapshot(Asset, Path, Error);
        if (!After) { Result->SetField(TEXT("after"), MakeShared<FJsonValueNull>()); return false; }
        Result->SetObjectField(TEXT("after"), After);
        for (const auto& Entry : Witness)
            if (TextRootValue(Entry.Key, Entry.Key->ContainerPtrToValuePtr<void>(Asset), Asset) != Entry.Value) return false;
        return After->GetIntegerField(TEXT("count")) == OldCount + Items->Num();
    };
    if (!Verify()) return FailAfter(TEXT("readback"), TEXT("Post-edit values/ownership/defaults changed; inspect applied draft before retrying; not saved"));
    if (Save)
    {
        bool AllowSave = true;
#if WITH_DEV_AUTOMATION_TESTS
        AllowSave = !FailArrayAppendSaveForTest;
#endif
        const bool Saved = AllowSave && UEditorAssetLibrary::SaveLoadedAsset(Asset, false)
            && !Asset->GetPackage()->IsDirty() && FPackageName::DoesPackageExist(AssetPath);
        Result->SetBoolField(TEXT("saved"), Saved);
        if (!Saved) return FailAfter(TEXT("save"), TEXT("Save failed; initialized array remains modified in memory; no rollback or automatic retry"));
        if (!Verify()) return FailAfter(TEXT("readback"), TEXT("Saved but post-save readback changed; inspect actual after and disk"));
    }
    Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
    Result->SetStringField(TEXT("stage"), Save ? TEXT("saved") : TEXT("applied")); return Result;
}

TSharedPtr<FJsonObject> Write(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UObject* Asset = LoadTarget(Params, Error);
    if (!Asset) return Failure(Error);
    bool Save = true;
    if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be boolean"));
    if (Save && Asset->GetOutermost()->IsDirty()) return Failure(TEXT("Target package already has unsaved changes; save explicitly first or use save=false"));
    const TArray<TSharedPtr<FJsonValue>>* Changes;
    if (!Params->TryGetArrayField(TEXT("changes"), Changes) || Changes->Num() < 1 || Changes->Num() > 64) return Failure(TEXT("changes must contain 1..64 patches"));
    FStagedValues Staged;
    TArray<FString> Paths;
    TArray<TSharedPtr<FJsonValue>> Receipts;
    int32 Budget = MaxValues;
    for (const TSharedPtr<FJsonValue>& Change : *Changes)
    {
        if (Change->Type != EJson::Object) return Failure(TEXT("Patch must be an object"));
        const TSharedPtr<FJsonObject> Patch = Change->AsObject();
        for (const auto& Field : Patch->Values)
            if (Field.Key != TEXT("path") && Field.Key != TEXT("value") && Field.Key != TEXT("expected_value")) return Failure(TEXT("Unknown patch key"));
        FString Path;
        if (!Patch->TryGetStringField(TEXT("path"), Path) || !Patch->HasField(TEXT("value"))) return Failure(TEXT("Patch requires path and value"));
        for (const FString& Previous : Paths)
            if (Path == Previous || Path.StartsWith(Previous + TEXT(".")) || Path.StartsWith(Previous + TEXT("["))
                || Previous.StartsWith(Path + TEXT(".")) || Previous.StartsWith(Path + TEXT("["))) return Failure(TEXT("Overlapping patch paths are not allowed"));
        FResolved Original;
        if (!Resolve(Asset, Path, Original, Error)) return Failure(Path + TEXT(": ") + Error);
        TSharedPtr<FJsonValue> Before = Encode(Original.Leaf, Original.Address, Error, Budget);
        if (!Before) return Failure(Path + TEXT(": ") + Error);
        if (Patch->HasField(TEXT("expected_value")))
        {
            FValueStorage Expected(Original.Leaf, Original.Address);
            if (!Decode(Original.Leaf, Expected.Data, Patch->Values[TEXT("expected_value")], Error, Budget)) return Failure(Path + TEXT(": expected_value: ") + Error);
            if (!Original.Leaf->Identical(Original.Address, Expected.Data)) return Failure(Path + TEXT(": expected_value conflict"), TEXT("conflict"));
        }
        if (!Staged.Contains(Original.Root)) Staged.Add(Original.Root, MakeShared<FValueStorage>(Original.Root, Original.Root->ContainerPtrToValuePtr<void>(Asset)));
        FResolved Pending;
        if (!Resolve(Asset, Path, Pending, Error, Staged[Original.Root]->Data)
            || !Decode(Pending.Leaf, Pending.Address, Patch->Values[TEXT("value")], Error, Budget)) return Failure(Path + TEXT(": ") + Error);
        TSharedPtr<FJsonObject> Receipt = MakeShared<FJsonObject>();
        Receipt->SetStringField(TEXT("path"), Path);
        Receipt->SetField(TEXT("before"), Before);
        Receipts.Add(MakeShared<FJsonValueObject>(Receipt));
        Paths.Add(Path);
    }
    int32 ConditionBudget = MaxValues;
    for (const FString& Path : Paths)
    {
        FResolved Original;
        FResolved Pending;
        if (!Resolve(Asset, Path, Original, Error)
            || !Resolve(Asset, Path, Pending, Error, Staged[Original.Root]->Data)
            || !ConditionsMet(Pending, Original.Address, &Staged, Error, ConditionBudget))
            return Failure(Path + TEXT(": ") + Error, TEXT("edit_condition"));
    }
    bool Modified = false;
    {
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "AssetProperties", "Edit DataAsset properties"));
        for (const auto& Entry : Staged)
        {
            void* Target = Entry.Key->ContainerPtrToValuePtr<void>(Asset);
            if (Entry.Key->Identical(Target, Entry.Value->Data)) continue;
            if (!Modified) Asset->Modify();
            Asset->PreEditChange(Entry.Key);
            Entry.Key->CopyCompleteValue(Target, Entry.Value->Data);
            Modified = true;
        }
        if (Modified)
        {
            Asset->MarkPackageDirty();
            for (const auto& Entry : Staged)
            {
                FPropertyChangedEvent Event(Entry.Key, EPropertyChangeType::ValueSet);
                Asset->PostEditChangeProperty(Event);
            }
        }
        else Transaction.Cancel();
    }
    Budget = MaxValues;
    for (int32 Index = 0; Index < Paths.Num(); ++Index)
    {
        FResolved Current;
        if (!Resolve(Asset, Paths[Index], Current, Error)) return Failure(TEXT("Post-edit readback failed: ") + Error, TEXT("readback"), Modified);
        TSharedPtr<FJsonValue> After = Encode(Current.Leaf, Current.Address, Error, Budget);
        if (!After) return Failure(TEXT("Post-edit readback failed: ") + Error, TEXT("readback"), Modified);
        Receipts[Index]->AsObject()->SetField(TEXT("after"), After);
    }
    const bool Saved = Save && UEditorAssetLibrary::SaveLoadedAsset(Asset, false)
        && !Asset->GetOutermost()->IsDirty() && FPackageName::DoesPackageExist(Asset->GetOutermost()->GetName());
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), !Save || Saved);
    Result->SetBoolField(TEXT("modified"), Modified);
    Result->SetBoolField(TEXT("saved"), Saved);
    Result->SetStringField(TEXT("asset_path"), Asset->GetOutermost()->GetName());
    Result->SetArrayField(TEXT("changes"), Receipts);
    Result->SetBoolField(TEXT("package_dirty"), Asset->GetOutermost()->IsDirty());
    if (Save && !Saved)
    {
        Result->SetStringField(TEXT("stage"), TEXT("save"));
        Result->SetStringField(TEXT("error"), TEXT("Save failed; in-memory edits may remain. No rollback was performed."));
    }
    return Result;
}
}