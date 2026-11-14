#include "Commands/UnrealMCPAnimationCommands.h"
#include "Commands/UnrealMCPAssetProperties.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/IAnimationSequenceCompiler.h"
#include "AssetCompilingManager.h"
#include "AnimationBlueprintLibrary.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/FileHelper.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraComponent.h"
#include "Tests/MCPAssetPropertyTestTypes.h"
#include <limits>

namespace UnrealMCPAnimation
{
    namespace
    {
        FString Json(const TSharedPtr<FJsonObject>& Value)
        {
            FString Text;
            FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
            return Text;
        }

        TSharedPtr<FJsonObject> Failure(const FString& Error)
        {
            auto Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("success"), false); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
            Result->SetStringField(TEXT("stage"), TEXT("preflight")); Result->SetStringField(TEXT("error"), Error); return Result;
        }

        void Seal(const TSharedRef<FJsonObject>& Result)
        {
            auto Payload = MakeShared<FJsonObject>(*Result);
            for (const auto* Name : {TEXT("baseline"), TEXT("package_dirty"), TEXT("semantics"), TEXT("success"), TEXT("modified"), TEXT("saved")}) Payload->RemoveField(Name);
            const auto Text = Json(Payload); const FTCHARToUTF8 Bytes(*Text); FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
            if (Result->GetBoolField(TEXT("complete"))) Result->SetStringField(TEXT("baseline"), Hash.ToString());
            else Result->SetField(TEXT("baseline"), MakeShared<FJsonValueNull>());
        }

        UAnimSequenceBase* Load(const TSharedPtr<FJsonObject>& Params, FString& Error)
        {
            FString Path;
            if (!Params || !Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
                || Path.Len() >= NAME_SIZE || Path.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Path))
            { Error = TEXT("Require an exact /Game animation package path"); return nullptr; }
            auto* Asset = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(Path));
            if (!Asset || (Asset->GetClass() != UAnimSequence::StaticClass() && Asset->GetClass() != UAnimMontage::StaticClass()) || Asset->GetPackage()->GetName() != Path)
            { Error = TEXT("Require an existing exact AnimSequence/AnimMontage, not a redirector or derived asset"); return nullptr; }
            return Asset;
        }

        struct FReader
        {
            const UObject* Root = nullptr;
            const UObject* TypedNotify = nullptr;
            int32 Values = 0;
            int32 Failures = 0;
            int32 StringCharacters = 0;
            TSet<const UObject*> Expanded;
            TMap<const UObject*, TSharedPtr<FJsonValue>> References;
            TArray<TSharedPtr<FJsonValue>> Unsupported;

            TSharedPtr<FJsonValue> Missing(const FString& Path, const FString& Reason)
            {
                ++Failures;
                if (Unsupported.Num() < 128)
                {
                    auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), Path); Entry->SetStringField(TEXT("reason"), Reason);
                    Unsupported.Add(MakeShared<FJsonValueObject>(Entry));
                }
                return MakeShared<FJsonValueNull>();
            }

            TSharedPtr<FJsonValue> Number(double Value, const FString& Path)
            {
                if (!FMath::IsFinite(Value)) return Missing(Path, TEXT("Nonfinite stored number"));
                return MakeShared<FJsonValueNumber>(Value);
            }

            TSharedPtr<FJsonValue> Reference(const UObject* Object)
            {
                if (!Object) return MakeShared<FJsonValueNull>();
                if (const auto* Previous = References.Find(Object)) return *Previous;
                auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("path"), Object->GetPathName());
                Result->SetStringField(TEXT("class_path"), Object->GetClass()->GetPathName());
                if (const auto* Skeleton = Cast<USkeleton>(Object)) Result->SetStringField(TEXT("skeleton_guid"), Skeleton->GetGuid().ToString());
                if (const auto* Sequence = Cast<UAnimSequence>(Object))
                {
                    if (const auto* Model = Sequence->GetDataModel()) Result->SetStringField(TEXT("source_data_guid"), Model->GenerateGuid().ToString());
                    Result->SetNumberField(TEXT("sampled_keys"), Sequence->GetNumberOfSampledKeys());
                }
                auto Value = MakeShared<FJsonValueObject>(Result); References.Add(Object, Value); return Value;
            }

            TSharedPtr<FJsonValue> Object(const UObject* Target, const FString& Path, int32 Depth)
            {
                if (!Target) return MakeShared<FJsonValueNull>();
                auto Result = MakeShared<FJsonObject>(*Reference(Target)->AsObject());
                Result->SetStringField(TEXT("outer_path"), Target->GetOuter()->GetPathName());
                if (const auto* Notify = Cast<UAnimNotify>(Target)) Result->SetBoolField(TEXT("native_branching_point"), Notify->bIsNativeBranchingPoint);
                if (const auto* State = Cast<UAnimNotifyState>(Target)) Result->SetBoolField(TEXT("native_branching_point"), State->bIsNativeBranchingPoint);
                if (Expanded.Contains(Target)) { Result->SetBoolField(TEXT("reference_only"), true); return MakeShared<FJsonValueObject>(Result); }
                if (Expanded.Num() >= 256 || Depth > 12) return Missing(Path, TEXT("Object expansion bound exceeded"));
                Expanded.Add(Target);
                const int32 Before = Failures;
                const FString Package = Target->GetClass()->GetPackage()->GetName();
                if (Target != TypedNotify && Package != TEXT("/Script/Engine") && Package != TEXT("/Script/NiagaraAnimNotifies")
                    && Target->GetClass()->GetMetaData(TEXT("MCPAnimationNotifyAuthoringSafe")) != TEXT("true"))
                    Missing(Path, TEXT("Custom notify may contain unreflected native data; fields are partial"));
                Result->SetObjectField(TEXT("fields"), Fields(Target->GetClass(), Target, Path, Depth + 1));
                Result->SetBoolField(TEXT("complete"), Before == Failures);
                return MakeShared<FJsonValueObject>(Result);
            }

            TSharedRef<FJsonObject> Fields(const UStruct* Type, const void* Data, const FString& Path, int32 Depth)
            {
                auto Result = MakeShared<FJsonObject>(); TArray<FProperty*> Properties;
                for (TFieldIterator<FProperty> Field(Type); Field; ++Field)
                    if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) Properties.Add(*Field);
                Properties.Sort([](const FProperty& Left, const FProperty& Right) { return Left.GetName() < Right.GetName(); });
                for (auto* Property : Properties)
                    Result->SetField(Property->GetName(), Value(Property, Property->ContainerPtrToValuePtr<void>(Data), Path + TEXT(".") + Property->GetName(), Depth + 1));
                return Result;
            }

            TSharedPtr<FJsonValue> Value(FProperty* Property, const void* Data, const FString& Path, int32 Depth)
            {
                if (++Values > 50000 || Depth > 16 || Property->ArrayDim != 1) return Missing(Path, TEXT("Value/depth/static-array bound exceeded"));
                if (!CastField<FEnumProperty>(Property) && !(CastField<FByteProperty>(Property) && CastField<FByteProperty>(Property)->Enum))
                {
                    if (const auto* Number = CastField<FNumericProperty>(Property))
                    {
                        const bool Unsigned = CastField<FUInt16Property>(Property) || CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property) || CastField<FByteProperty>(Property);
                        const double Numeric = Number->IsFloatingPoint() ? Number->GetFloatingPointPropertyValue(Data)
                            : Unsigned ? static_cast<double>(Number->GetUnsignedIntPropertyValue(Data)) : static_cast<double>(Number->GetSignedIntPropertyValue(Data));
                        if (!FMath::IsFinite(Numeric) || (Number->IsInteger() && FMath::Abs(Numeric) > 9007199254740991.0)) return Missing(Path, TEXT("Nonfinite or non-lossless number"));
                        return MakeShared<FJsonValueNumber>(Numeric);
                    }
                }
                if (const auto* ReferenceProperty = CastField<FObjectPropertyBase>(Property))
                {
                    if (CastField<FSoftObjectProperty>(Property))
                    {
                        FString Error; auto Result = UnrealMCPAssetProperties::EncodeValue(Property, Data, Error);
                        return Error.IsEmpty() ? Result : Missing(Path, Error);
                    }
                    const auto* Target = ReferenceProperty->GetObjectPropertyValue(Data);
                    if (Target && (Target->IsA<UAnimNotify>() || Target->IsA<UAnimNotifyState>())) return Object(Target, Path, Depth + 1);
                    if (Target && Target != Root && !Target->IsAsset() && !Target->IsA<UClass>()) return Missing(Path, TEXT("Unsupported non-asset object reference"));
                    return Reference(Target);
                }
                if (const auto* Array = CastField<FArrayProperty>(Property))
                {
                    FScriptArrayHelper Helper(Array, Data); TArray<TSharedPtr<FJsonValue>> Items;
                    if (Helper.Num() > 4096) return Missing(Path, TEXT("Array exceeds 4096 elements"));
                    for (int32 Index = 0; Index < Helper.Num(); ++Index)
                    {
                        if (Values >= 50000) { Missing(Path, TEXT("Value bound exceeded")); break; }
                        Items.Add(Value(Array->Inner, Helper.GetRawPtr(Index), Path + FString::Printf(TEXT("[%d]"), Index), Depth + 1));
                    }
                    return MakeShared<FJsonValueArray>(Items);
                }
                if (const auto* Structure = CastField<FStructProperty>(Property))
                {
                    if (!TFieldIterator<FProperty>(Structure->Struct)) return Missing(Path, TEXT("Struct has unreflected storage"));
                    return MakeShared<FJsonValueObject>(Fields(Structure->Struct, Data, Path, Depth + 1));
                }
                FString Error;
                if (CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property)) return Missing(Path, TEXT("Map/set not supported in this authoring scope"));
                auto Result = UnrealMCPAssetProperties::EncodeValue(Property, Data, Error);
                if (!Error.IsEmpty()) return Missing(Path, Error);
                FString Text;
                if (Result && Result->TryGetString(Text))
                {
                    if (Text.Len() > 65536 || StringCharacters > 524288 - Text.Len()) return Missing(Path, TEXT("String budget exceeded"));
                    StringCharacters += Text.Len();
                }
                return Result;
            }

            void Selected(const UObject* Asset, const TArray<FName>& Names, const TSharedRef<FJsonObject>& Result)
            {
                for (const auto& Name : Names)
                {
                    auto* Property = Asset->GetClass()->FindPropertyByName(Name);
                    if (!Property) Result->SetField(Name.ToString(), Missing(Name.ToString(), TEXT("Property unavailable in this engine")));
                    else Result->SetField(Name.ToString(), Value(Property, Property->ContainerPtrToValuePtr<void>(Asset), Name.ToString(), 0));
                }
            }
        };

        TSharedRef<FJsonObject> Snapshot(const UAnimSequenceBase* Asset, const UObject* TypedNotify = nullptr)
        {
            FReader Reader; Reader.Root = Asset; Reader.TypedNotify = TypedNotify; auto Result = MakeShared<FJsonObject>();
            Result->SetNumberField(TEXT("animation_contract"), 1); Result->SetStringField(TEXT("asset_path"), Asset->GetPackage()->GetName());
            Result->SetStringField(TEXT("object_path"), Asset->GetPathName()); Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
            Result->SetField(TEXT("skeleton"), Reader.Reference(Asset->GetSkeleton()));
            Result->SetField(TEXT("length"), Reader.Number(Asset->GetPlayLength(), TEXT("length"))); Result->SetField(TEXT("rate_scale"), Reader.Number(Asset->RateScale, TEXT("rate_scale")));
            auto Settings = MakeShared<FJsonObject>();
            if (const auto* Sequence = Cast<UAnimSequence>(Asset))
            {
                Reader.Selected(Sequence, {TEXT("AdditiveAnimType"), TEXT("RefPoseType"), TEXT("RefPoseSeq"), TEXT("RefFrameIndex"), TEXT("bEnableRootMotion"), TEXT("bForceRootLock"), TEXT("RootMotionRootLock")}, Settings);
                Result->SetNumberField(TEXT("sampled_keys"), Sequence->GetNumberOfSampledKeys());
                const auto Rate = Sequence->GetSamplingFrameRate(); Result->SetNumberField(TEXT("sampling_rate_numerator"), Rate.Numerator); Result->SetNumberField(TEXT("sampling_rate_denominator"), Rate.Denominator);
                const auto* Model = Sequence->GetDataModel();
                if (Model) { Result->SetStringField(TEXT("source_data_guid"), Model->GenerateGuid().ToString()); Result->SetNumberField(TEXT("source_keys"), Model->GetNumberOfKeys()); }
                else Reader.Missing(TEXT("source_data"), TEXT("No animation DataModel available"));
            }
            else Reader.Selected(Asset, {TEXT("SlotAnimTracks"), TEXT("CompositeSections"), TEXT("BlendIn"), TEXT("BlendOut"), TEXT("BlendOutTriggerTime"), TEXT("bEnableAutoBlendOut"), TEXT("SyncSlotIndex")}, Settings);
            Result->SetObjectField(TEXT("settings"), Settings);
            TArray<TSharedPtr<FJsonValue>> Tracks;
            for (int32 Index = 0; Index < FMath::Min(Asset->AnimNotifyTracks.Num(), 256); ++Index)
            {
                auto Track = MakeShared<FJsonObject>(); Track->SetNumberField(TEXT("index"), Index); Track->SetStringField(TEXT("name"), Asset->AnimNotifyTracks[Index].TrackName.ToString());
                const auto Color = Asset->AnimNotifyTracks[Index].TrackColor;
                Track->SetArrayField(TEXT("color"), {MakeShared<FJsonValueNumber>(Color.R), MakeShared<FJsonValueNumber>(Color.G), MakeShared<FJsonValueNumber>(Color.B), MakeShared<FJsonValueNumber>(Color.A)});
                Tracks.Add(MakeShared<FJsonValueObject>(Track));
            }
            if (Asset->AnimNotifyTracks.Num() > 256) Reader.Missing(TEXT("notify_tracks"), TEXT("Track bound exceeded"));
            Result->SetArrayField(TEXT("notify_tracks"), Tracks);
            TArray<TSharedPtr<FJsonValue>> Events;
            for (int32 Index = 0; Index < FMath::Min(Asset->Notifies.Num(), 4096); ++Index)
            {
                const auto& Event = Asset->Notifies[Index]; const FString Path = FString::Printf(TEXT("notifies[%d]"), Index);
                const int32 Before = Reader.Failures;
                auto Row = MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("index"), Index);
                Row->SetStringField(TEXT("guid"), Event.Guid.ToString(EGuidFormats::DigitsWithHyphens)); Row->SetNumberField(TEXT("track_index"), Event.TrackIndex);
                if (Asset->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)) Row->SetStringField(TEXT("track_name"), Asset->AnimNotifyTracks[Event.TrackIndex].TrackName.ToString());
                else Reader.Missing(Path, TEXT("Invalid notify track index"));
                Row->SetField(TEXT("time"), Reader.Number(Event.GetTime(), Path + TEXT(".time"))); Row->SetField(TEXT("trigger_time"), Reader.Number(Event.GetTriggerTime(), Path + TEXT(".trigger_time")));
                Row->SetField(TEXT("end_trigger_time"), Reader.Number(Event.GetEndTriggerTime(), Path + TEXT(".end_trigger_time"))); Row->SetBoolField(TEXT("effective_branching_point"), Event.IsBranchingPoint());
                Row->SetObjectField(TEXT("fields"), Reader.Fields(FAnimNotifyEvent::StaticStruct(), &Event, Path, 0));
                Row->SetBoolField(TEXT("complete"), Before == Reader.Failures); Events.Add(MakeShared<FJsonValueObject>(Row));
            }
            if (Asset->Notifies.Num() > 4096) Reader.Missing(TEXT("notifies"), TEXT("Notify bound exceeded"));
            Result->SetArrayField(TEXT("notifies"), Events); Result->SetNumberField(TEXT("notify_count"), Asset->Notifies.Num());
            Result->SetBoolField(TEXT("complete"), Reader.Failures == 0); Result->SetNumberField(TEXT("unsupported_count"), Reader.Failures); Result->SetArrayField(TEXT("unsupported"), Reader.Unsupported);
            Seal(Result);
            Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
            Result->SetStringField(TEXT("semantics"), TEXT("Stored animation authoring scope, not full raw animation reconstruction or runtime playback. No preview/play/save. Unsupported data blocks writes."));
            return Result;
        }

        UAnimSequenceBase* Prepare(const TSharedPtr<FJsonObject>& Params, const TSet<FString>& Allowed, TSharedPtr<FJsonObject>& Before, FString& Error)
        {
            if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0
                || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid()))
            { Error = TEXT("Require an idle Editor with no asset compilation, outside PIE/save/GC/modal/slow tasks"); return nullptr; }
            if (!Params) { Error = TEXT("Missing arguments"); return nullptr; }
            for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) { Error = TEXT("Unknown argument: ") + Pair.Key; return nullptr; }
            auto* Asset = Load(Params, Error); if (!Asset) return nullptr;
            if (Asset->GetSkeleton() && Asset->GetSkeleton()->GetPackage()->IsDirty()) { Error = TEXT("Referenced Skeleton has unsaved author edits; refuse without saving it"); return nullptr; }
            if (Asset->GetPackage()->IsDirty()) { Error = TEXT("Preexisting dirty target refused; do not save user drafts implicitly"); return nullptr; }
            Before = Snapshot(Asset); FString Baseline;
            if (!Before->GetBoolField(TEXT("complete"))) { Error = TEXT("Incomplete snapshot; review unsupported fields before authoring"); return nullptr; }
            if (!Params->TryGetStringField(TEXT("expected_baseline"), Baseline) || Baseline != Before->GetStringField(TEXT("baseline")))
            { Error = TEXT("Missing/stale expected_baseline; read and review again"); return nullptr; }
            return Asset;
        }

        bool Flags(const TSharedPtr<FJsonObject>& Params, bool& DryRun, bool& SaveAsset, FString& Error)
        {
            DryRun = true; SaveAsset = false;
            if ((Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun))
                || (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), SaveAsset)))
            { Error = TEXT("dry_run and save must be bool"); return false; }
            return true;
        }

        TSharedRef<FJsonObject> Receipt(const TSharedPtr<FJsonObject>& Before, bool DryRun, bool Changed)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("animation_contract"), 1);
            Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), Changed);
            Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), Before->GetBoolField(TEXT("package_dirty")));
            Result->SetObjectField(TEXT("before"), Before); Result->SetObjectField(TEXT("after"), Before); return Result;
        }

#if WITH_DEV_AUTOMATION_TESTS
        bool FailSaveForTest = false;
#endif
        void Finish(UAnimSequenceBase* Asset, const TSharedRef<FJsonObject>& Result, bool SaveAsset)
        {
            bool Saved = false;
            if (Result->GetBoolField(TEXT("success")) && SaveAsset)
            {
#if WITH_DEV_AUTOMATION_TESTS
                if (!FailSaveForTest)
#endif
                    Saved = UEditorAssetLibrary::SaveLoadedAsset(Asset, false) && !Asset->GetPackage()->IsDirty();
                if (!Saved)
                {
                    Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save"));
                    Result->SetStringField(TEXT("error"), TEXT("Applied changes remain; target save failed, no rollback or retry"));
                }
            }
            Result->SetBoolField(TEXT("saved"), Saved); Result->SetObjectField(TEXT("after"), Snapshot(Asset));
            Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
        }
    }

    namespace
    {
        struct FNotifyLeaf
        {
            FString Path;
            FProperty* Root = nullptr;
            FProperty* Property = nullptr;
            void* Address(UObject* Notify, void* Storage = nullptr) const
            {
                void* RootValue = Storage ? Storage : Root->ContainerPtrToValuePtr<void>(Notify);
                return Root == Property ? RootValue : Property->ContainerPtrToValuePtr<void>(RootValue);
            }
        };

        struct FNotifyStorage
        {
            FProperty* Property;
            void* Data;
            explicit FNotifyStorage(FProperty* InProperty, UObject* Notify) : Property(InProperty)
            {
                Data = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
                Property->InitializeValue(Data);
                Property->CopyCompleteValue(Data, Property->ContainerPtrToValuePtr<void>(Notify));
            }
            ~FNotifyStorage() { Property->DestroyValue(Data); FMemory::Free(Data); }
        };

        class FNotifyWitness final : public FObjectAndNameAsStringProxyArchive
        {
            const TSet<const FProperty*>& Excluded;
        public:
            FNotifyWitness(FArchive& Inner, const TSet<const FProperty*>& InExcluded)
                : FObjectAndNameAsStringProxyArchive(Inner, false), Excluded(InExcluded)
            { ArNoDelta = true; }
            virtual bool ShouldSkipProperty(const FProperty* Property) const override
            { return Excluded.Contains(Property) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property); }
            virtual void Serialize(void* Data, int64 Length) override
            {
                if (Length < 0 || Tell() > 1048576 - Length) { SetError(); return; }
                FObjectAndNameAsStringProxyArchive::Serialize(Data, Length);
            }
        };

        FString NotifyWitness(UObject* Object, const TSet<const FProperty*>& Excluded, FString& Error)
        {
            TArray<uint8> Bytes; FMemoryWriter Memory(Bytes); FNotifyWitness Writer(Memory, Excluded);
            Object->Serialize(Writer);
            if (Writer.IsError() || Bytes.Num() > 1048576) { Error = TEXT("Serialized protection witness exceeds bounds or cannot be read"); return {}; }
            FSHAHash Hash; FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash); return Hash.ToString();
        }

        bool NotifyLeaves(UAnimNotify* Notify, TArray<FNotifyLeaf>& Leaves, FString& Error)
        {
            UClass* Class = Notify->GetClass();
            if (Class->GetSuperClass() != UAnimNotify::StaticClass() || !Class->HasAnyClassFlags(CLASS_Native)
                || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_CompiledFromBlueprint)
                || (Class->GetPathName() != TEXT("/Script/NodeFall.NFAnimNotify_PlayNiagaraAppearance")
                    && Class->GetMetaData(TEXT("MCPNiagaraAppearanceNotifyAuthoringSafe")) != TEXT("true")))
            { Error = TEXT("Require the finite native Niagara-appearance adapter or explicit MCPNiagaraAppearanceNotifyAuthoringSafe=true opt-in; no arbitrary class loading"); return false; }
            auto Add = [&](const TCHAR* Name, const TCHAR* Kind, const TCHAR* Member = nullptr)
            {
                FProperty* Root = Class->FindPropertyByName(FName(Name));
                auto* Structure = CastField<FStructProperty>(Root);
                FProperty* Property = Member && Structure ? Structure->Struct->FindPropertyByName(FName(Member)) : Member ? nullptr : Root;
                if (!Root || !Property || Root->ArrayDim != 1 || Property->ArrayDim != 1
                    || !Root->HasAnyPropertyFlags(CPF_Edit) || !Property->HasAnyPropertyFlags(CPF_Edit)
                    || Root->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated | CPF_InstancedReference | CPF_ContainsInstancedReference)
                    || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated | CPF_InstancedReference | CPF_ContainsInstancedReference)
                    || Root->HasMetaData(TEXT("EditCondition")) || Property->HasMetaData(TEXT("EditCondition"))
                    || !Notify->CanEditChange(Root)) return false;
                auto* Struct = CastField<FStructProperty>(Property);
                auto* Object = CastField<FObjectProperty>(Property);
                const FString Type(Kind);
                const bool Valid = (Type == TEXT("name") && CastField<FNameProperty>(Property))
                    || (Type == TEXT("float") && CastField<FFloatProperty>(Property)) || (Type == TEXT("bool") && CastField<FBoolProperty>(Property))
                    || (Type == TEXT("vector") && Struct && Struct->Struct == TBaseStructure<FVector>::Get())
                    || (Type == TEXT("rotator") && Struct && Struct->Struct == TBaseStructure<FRotator>::Get())
                    || (Type == TEXT("system") && Object && Object->PropertyClass == UNiagaraSystem::StaticClass());
                if (!Valid) return false;
                Leaves.Add({Member ? FString(Name) + TEXT(".") + Member : FString(Name), Root, Property}); return true;
            };
            if (!Add(TEXT("Template"), TEXT("system")) || !Add(TEXT("SocketName"), TEXT("name"))
                || !Add(TEXT("bAttached"), TEXT("bool")) || !Add(TEXT("bAbsoluteScale"), TEXT("bool"))
                || !Add(TEXT("LocationOffset"), TEXT("vector")) || !Add(TEXT("RotationOffset"), TEXT("rotator")) || !Add(TEXT("Scale"), TEXT("vector"))
                || !Add(TEXT("Appearance"), TEXT("name"), TEXT("SizeParameter")) || !Add(TEXT("Appearance"), TEXT("float"), TEXT("SizeMultiplier")))
            { Error = TEXT("Native Niagara-appearance field shape/editability differs from the finite adapter"); return false; }
            return true;
        }

        UAnimMontage* NotifyTarget(const TSharedPtr<FJsonObject>& Params, UAnimNotify*& Notify, FGuid& Guid, TArray<FNotifyLeaf>& Leaves, FString& Error)
        {
            auto* Montage = Cast<UAnimMontage>(Load(Params, Error)); FString Text;
            if (!Montage || !Params->TryGetStringField(TEXT("event_guid"), Text) || !FGuid::ParseExact(Text, EGuidFormats::DigitsWithHyphens, Guid) || !Guid.IsValid())
            { Error = TEXT("Require an existing exact Montage and nonzero hyphenated event GUID"); return nullptr; }
            TSet<FGuid> Seen; Notify = nullptr;
            for (const auto& Event : Montage->Notifies)
            {
                if (!Event.Guid.IsValid() || Seen.Contains(Event.Guid)) { Error = TEXT("Missing/duplicate event GUID prevents safe identity protection"); return nullptr; }
                Seen.Add(Event.Guid);
                if (Event.Guid == Guid)
                {
                    if (Event.NotifyStateClass || !Event.Notify || Event.Notify->GetOuter() != Montage)
                    { Error = TEXT("Require one instant Notify owned directly by the selected Montage"); return nullptr; }
                    Notify = Event.Notify;
                }
            }
            if (!Notify || !NotifyLeaves(Notify, Leaves, Error)) { if (Error.IsEmpty()) Error = TEXT("Event GUID not found"); return nullptr; }
            int32 References = 0;
            for (const auto& Event : Montage->Notifies) if (Event.Notify == Notify) ++References;
            if (References != 1) { Error = TEXT("Notify object is shared by multiple events"); return nullptr; }
            return Montage;
        }

        TSharedPtr<FJsonObject> NotifyScope(UAnimMontage* Montage, UAnimNotify* Notify, const FGuid& Guid, const TArray<FNotifyLeaf>& Leaves, FString& Error)
        {
            auto Scope = Snapshot(Montage, Notify);
            if (!Scope->GetBoolField(TEXT("complete"))) { Error = TEXT("Other unsupported authoring data prevents a protected Notify patch"); return nullptr; }
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("notify_properties_contract"), 1);
            Result->SetStringField(TEXT("asset_path"), Montage->GetPackage()->GetName()); Result->SetStringField(TEXT("event_guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
            Result->SetStringField(TEXT("notify_class"), Notify->GetClass()->GetPathName()); Result->SetStringField(TEXT("outer_path"), Notify->GetOuter()->GetPathName());
            Result->SetStringField(TEXT("object_path"), Notify->GetPathName()); Result->SetObjectField(TEXT("animation"), Scope);
            TArray<TSharedPtr<FJsonValue>> Fields;
            for (const auto& Leaf : Leaves)
            {
                auto Field = MakeShared<FJsonObject>(); Field->SetStringField(TEXT("path"), Leaf.Path);
                Field->SetStringField(TEXT("cpp_type"), Leaf.Property->GetCPPType()); Field->SetBoolField(TEXT("writable"), true);
                Field->SetField(TEXT("value"), UnrealMCPAssetProperties::EncodeValue(Leaf.Property, Leaf.Address(Notify), Error));
                if (!Error.IsEmpty()) return nullptr;
                if (Leaf.Property->HasMetaData(TEXT("ClampMin"))) Field->SetStringField(TEXT("clamp_min"), Leaf.Property->GetMetaData(TEXT("ClampMin")));
                if (Leaf.Path == TEXT("Template")) Field->SetStringField(TEXT("reference_class"), UNiagaraSystem::StaticClass()->GetPathName());
                Fields.Add(MakeShared<FJsonValueObject>(Field));
            }
            Result->SetArrayField(TEXT("editable_fields"), Fields);
            auto Witnesses = MakeShared<FJsonObject>();
            Witnesses->SetStringField(Montage->GetPathName(), NotifyWitness(Montage, {}, Error));
            for (const auto& Event : Montage->Notifies)
                for (UObject* Object : {static_cast<UObject*>(Event.Notify.Get()), static_cast<UObject*>(Event.NotifyStateClass.Get())})
                    if (Object) Witnesses->SetStringField(Object->GetPathName(), NotifyWitness(Object, {}, Error));
            if (!Error.IsEmpty()) return nullptr;
            Result->SetObjectField(TEXT("stored_witnesses"), Witnesses);
            const FString File = FPackageName::LongPackageNameToFilename(Montage->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
            Result->SetStringField(TEXT("saved_file_hash"), LexToString(FMD5Hash::HashFile(*File)));
            Result->SetBoolField(TEXT("complete"), true); Seal(Result); Result->SetBoolField(TEXT("package_dirty"), Montage->GetPackage()->IsDirty());
            Result->SetStringField(TEXT("semantics"), TEXT("Finite Niagara-appearance patch contract; reflected authoring plus bounded serialized witnesses. Legacy animation completeness is unchanged. No runtime playback proof."));
            return Result;
        }
    }

    TSharedPtr<FJsonObject> ReadNotifyProperties(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; UAnimNotify* Notify = nullptr; FGuid Guid; TArray<FNotifyLeaf> Leaves;
        if (Params) for (const auto& Pair : Params->Values)
            if (Pair.Key != TEXT("asset_path") && Pair.Key != TEXT("event_guid")) return Failure(TEXT("Unknown argument: ") + Pair.Key);
        auto* Montage = NotifyTarget(Params, Notify, Guid, Leaves, Error); if (!Montage) return Failure(Error);
        auto Result = NotifyScope(Montage, Notify, Guid, Leaves, Error); if (!Result) return Failure(Error);
        Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
    }

    TSharedPtr<FJsonObject> PatchNotifyProperties(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error;
        if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask
            || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0 || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid()))
            return Failure(FString::Printf(TEXT("Require idle Editor: game_thread=%d editor=%d PIE=%d simulation=%d save=%d GC=%d slow_task=%d compiling=%d modal=%d"),
                IsInGameThread(), GEditor != nullptr, GEditor && GEditor->PlayWorld != nullptr, GEditor && GEditor->bIsSimulatingInEditor,
                !!GIsSavingPackage, IsGarbageCollecting(), !!GIsSlowTask, FAssetCompilingManager::Get().GetNumRemainingAssets(),
                FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid()));
        const TSet<FString> Allowed{TEXT("asset_path"), TEXT("event_guid"), TEXT("expected_class"), TEXT("expected_outer"), TEXT("expected_baseline"), TEXT("changes"), TEXT("dry_run"), TEXT("save")};
        if (!Params || Json(Params).Len() > 65536) return Failure(TEXT("Missing/oversized request; limit 64Ki characters"));
        for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) return Failure(TEXT("Unknown argument: ") + Pair.Key);
        UAnimNotify* Notify = nullptr; FGuid Guid; TArray<FNotifyLeaf> Leaves;
        auto* Montage = NotifyTarget(Params, Notify, Guid, Leaves, Error); if (!Montage) return Failure(Error);
        if (Montage->GetPackage()->IsDirty() || (Montage->GetSkeleton() && Montage->GetSkeleton()->GetPackage()->IsDirty())) return Failure(TEXT("Preexisting dirty Montage/Skeleton refused"));
        auto Before = NotifyScope(Montage, Notify, Guid, Leaves, Error); if (!Before) return Failure(Error);
        FString Class, Outer, Baseline;
        if (!Params->TryGetStringField(TEXT("expected_class"), Class) || Class != Notify->GetClass()->GetPathName()
            || !Params->TryGetStringField(TEXT("expected_outer"), Outer) || Outer != Montage->GetPathName()
            || !Params->TryGetStringField(TEXT("expected_baseline"), Baseline) || Baseline != Before->GetStringField(TEXT("baseline")))
            return Failure(TEXT("Class/Outer mismatch or missing/stale baseline; read the dedicated contract again"));
        bool DryRun, SaveAsset; if (!Flags(Params, DryRun, SaveAsset, Error)) return Failure(Error);
        const TArray<TSharedPtr<FJsonValue>>* Changes = nullptr;
        if (!Params->TryGetArrayField(TEXT("changes"), Changes) || Changes->Num() < 1 || Changes->Num() > 32) return Failure(TEXT("Require 1..32 unique supported leaf changes"));
        TMap<FProperty*, TUniquePtr<FNotifyStorage>> Staged; TSet<FString> Paths;
        for (const auto& Value : *Changes)
        {
            const TSharedPtr<FJsonObject>* Change = nullptr; FString Path;
            if (!Value->TryGetObject(Change) || (*Change)->Values.Num() != 2 || !(*Change)->TryGetStringField(TEXT("path"), Path) || !(*Change)->HasField(TEXT("value")) || Paths.Contains(Path))
                return Failure(TEXT("Each change requires exactly one unique path and value"));
            const auto* Leaf = Leaves.FindByPredicate([&](const FNotifyLeaf& Field) { return Field.Path == Path; });
            if (!Leaf) return Failure(TEXT("Unsupported Notify leaf: ") + Path);
            Paths.Add(Path);
            if (!Staged.Contains(Leaf->Root)) Staged.Add(Leaf->Root, MakeUnique<FNotifyStorage>(Leaf->Root, Notify));
            if (!UnrealMCPAssetProperties::DecodeValue(Leaf->Property, Leaf->Address(Notify, Staged[Leaf->Root]->Data), (*Change)->Values[TEXT("value")], Error)) return Failure(Path + TEXT(": ") + Error);
        }
        auto Address = [&](const TCHAR* Path)
        {
            const auto& Leaf = *Leaves.FindByPredicate([&](const FNotifyLeaf& Field) { return Field.Path == Path; });
            return Leaf.Address(Notify, Staged.Contains(Leaf.Root) ? Staged[Leaf.Root]->Data : nullptr);
        };
        const auto* Template = CastField<FObjectProperty>(Leaves[0].Property);
        auto* System = Cast<UNiagaraSystem>(Template->GetObjectPropertyValue(Address(TEXT("Template"))));
        const auto SizeName = *static_cast<FName*>(Address(TEXT("Appearance.SizeParameter")));
        const float Size = *static_cast<float*>(Address(TEXT("Appearance.SizeMultiplier")));
        const auto Scale = *static_cast<FVector*>(Address(TEXT("Scale")));
        if (!System || System->GetPackage()->IsDirty() || !FMath::IsFinite(Size) || Size < 0.01f || Scale.GetMin() < 0.01 || Scale.ContainsNaN()
            || (SizeName.IsNone() && Size != 1) || (!SizeName.IsNone() && (!SizeName.ToString().StartsWith(TEXT("User."))
                || System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), SizeName)) == INDEX_NONE)))
            return Failure(TEXT("Require a saved Niagara Template, positive Scale >=0.01, Size >=0.01 and an existing User Float; None requires multiplier 1"));
        FReader ProposalReader; ProposalReader.Root = Montage; ProposalReader.TypedNotify = Notify;
        TSharedPtr<FJsonObject> Proposed;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json(Before->GetObjectField(TEXT("animation")))), Proposed)) return Failure(TEXT("Cannot stage protected snapshot"));
        auto Rows = Proposed->GetArrayField(TEXT("notifies"));
        TSharedPtr<FJsonObject> Fields;
        for (const auto& Row : Rows) if (Row->AsObject()->GetStringField(TEXT("guid")) == Guid.ToString(EGuidFormats::DigitsWithHyphens))
            Fields = Row->AsObject()->GetObjectField(TEXT("fields"))->GetObjectField(TEXT("Notify"))->GetObjectField(TEXT("fields"));
        if (!Fields) return Failure(TEXT("Cannot stage selected event"));
        bool Changed = false; TSet<const FProperty*> Excluded;
        for (const auto& Leaf : Leaves) if (Paths.Contains(Leaf.Path)) Excluded.Add(Leaf.Property);
        for (const auto& Pair : Staged)
        {
            Changed |= !Pair.Key->Identical(Pair.Key->ContainerPtrToValuePtr<void>(Notify), Pair.Value->Data);
            Fields->SetField(Pair.Key->GetName(), ProposalReader.Value(Pair.Key, Pair.Value->Data, Pair.Key->GetName(), 0));
        }
        if (ProposalReader.Failures) return Failure(TEXT("Staged values cannot be read completely"));
        auto Result = Receipt(Before, DryRun, Changed); Result->SetNumberField(TEXT("notify_properties_contract"), 1); Result->SetStringField(TEXT("event_guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
        Result->SetObjectField(TEXT("proposed"), Proposed);
        if (DryRun || !Changed) return Result;
        const FString Protected = NotifyWitness(Notify, Excluded, Error); if (!Error.IsEmpty()) return Failure(Error);
        auto Normalize = [](const TSharedPtr<FJsonObject>& Scope)
        {
            auto Copy = MakeShared<FJsonObject>(*Scope);
            for (const auto* Name : {TEXT("baseline"), TEXT("package_dirty"), TEXT("semantics")}) Copy->RemoveField(Name);
            return Json(Copy);
        };
        const FString Expected = Normalize(Proposed);
        const auto Current = NotifyScope(Montage, Notify, Guid, Leaves, Error);
        if (!Current || Current->GetStringField(TEXT("baseline")) != Baseline || Montage->GetPackage()->IsDirty()
            || System->GetPackage()->IsDirty() || FAssetCompilingManager::Get().GetNumRemainingAssets() != 0)
            return Failure(TEXT("Target/reference changed or compilation started during preflight; read again"));
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "MontageNotifyProperties", "Patch montage Niagara notify properties"));
            Montage->SetFlags(RF_Transactional); Notify->SetFlags(RF_Transactional); Montage->Modify(); Notify->Modify();
            for (const auto& Pair : Staged) Notify->PreEditChange(Pair.Key);
            for (const auto& Pair : Staged) Pair.Key->CopyCompleteValue(Pair.Key->ContainerPtrToValuePtr<void>(Notify), Pair.Value->Data);
            for (const auto& Pair : Staged)
            {
                FPropertyChangedEvent Event(Pair.Key, EPropertyChangeType::ValueSet); Notify->PostEditChangeProperty(Event);
            }
            static_cast<UAnimSequenceBase*>(Montage)->RefreshCacheData(); Montage->MarkPackageDirty();
        }
        Result->SetBoolField(TEXT("modified"), true);
        auto Verify = [&]()
        {
            Error.Empty(); auto After = NotifyScope(Montage, Notify, Guid, Leaves, Error);
            if (After) Result->SetObjectField(TEXT("after"), After);
            bool Preserved = After && Normalize(After->GetObjectField(TEXT("animation"))) == Expected && NotifyWitness(Notify, Excluded, Error) == Protected;
            if (After)
                for (const auto& Pair : Before->GetObjectField(TEXT("stored_witnesses"))->Values)
                    if (Pair.Key != Notify->GetPathName()) Preserved &= After->GetObjectField(TEXT("stored_witnesses"))->GetStringField(Pair.Key) == Pair.Value->AsString();
            Result->SetBoolField(TEXT("existing_data_preserved"), Preserved); Result->SetBoolField(TEXT("package_dirty"), Montage->GetPackage()->IsDirty());
            if (!Preserved) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback")); Result->SetStringField(TEXT("error"), TEXT("Protected author data differs after edit/save; actual state remains, no rollback/retry. ") + Error); }
            return Preserved;
        };
        if (!Verify() || !SaveAsset) return Result;
        bool Saved = false;
#if WITH_DEV_AUTOMATION_TESTS
        if (!FailSaveForTest)
#endif
            Saved = UEditorAssetLibrary::SaveLoadedAsset(Montage, false) && !Montage->GetPackage()->IsDirty();
        Result->SetBoolField(TEXT("saved"), Saved);
        Verify();
        if (!Saved && Result->GetBoolField(TEXT("success"))) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save")); Result->SetStringField(TEXT("error"), TEXT("Applied Notify changes remain; target save failed")); }
        return Result;
    }

    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; const auto* Asset = Load(Params, Error);
        if (!Asset) return Failure(Error);
        auto Result = Snapshot(Asset); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
    }

    TSharedPtr<FJsonObject> SetAdditive(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; TSharedPtr<FJsonObject> Before;
        auto* Sequence = Cast<UAnimSequence>(Prepare(Params, {TEXT("asset_path"), TEXT("expected_baseline"), TEXT("settings"), TEXT("dry_run"), TEXT("save")}, Before, Error));
        if (!Sequence) return Failure(Error.IsEmpty() ? TEXT("Additive edits require AnimSequence, not Montage") : Error);
        if (!Sequence->GetSkeleton() || !Sequence->GetDataModel() || Sequence->GetDataModel()->GetNumberOfKeys() < 1)
            return Failure(TEXT("Require a valid Skeleton, source data and idle target compression"));
        bool DryRun, SaveAsset; if (!Flags(Params, DryRun, SaveAsset, Error)) return Failure(Error);
        const TSharedPtr<FJsonObject>* Settings = nullptr;
        if (!Params->TryGetObjectField(TEXT("settings"), Settings) || (*Settings)->Values.IsEmpty()) return Failure(TEXT("settings must contain confirmed Additive fields"));
        const TSet<FString> Allowed{TEXT("AdditiveAnimType"), TEXT("RefPoseType"), TEXT("RefPoseSeq"), TEXT("RefFrameIndex")};
        auto NewSettings = MakeShared<FJsonObject>(*Before->GetObjectField(TEXT("settings")));
        for (const auto& Pair : (*Settings)->Values)
        {
            if (!Allowed.Contains(Pair.Key)) return Failure(TEXT("Unsupported Additive field: ") + Pair.Key);
            NewSettings->SetField(Pair.Key, Pair.Value);
        }
        auto EnumValue = [&Error, &NewSettings](const TCHAR* Name, const UEnum* Enum, int64& Value)
        {
            FString Text; if (!NewSettings->TryGetStringField(Name, Text)) { Error = FString(Name) + TEXT(" must be an internal enum name"); return false; }
            Value = Enum->GetValueByNameString(Text);
            if (Value == INDEX_NONE || Enum->GetNameStringByValue(Value) != Text || Text.EndsWith(TEXT("MAX")))
            { Error = FString(Name) + TEXT(" has invalid/noncanonical enum value"); return false; }
            return true;
        };
        int64 Additive, Pose;
        if (!EnumValue(TEXT("AdditiveAnimType"), StaticEnum<EAdditiveAnimationType>(), Additive) || !EnumValue(TEXT("RefPoseType"), StaticEnum<EAdditiveBasePoseType>(), Pose)) return Failure(Error);
        double Frame;
        if (!NewSettings->TryGetNumberField(TEXT("RefFrameIndex"), Frame) || !FMath::IsFinite(Frame) || Frame < 0 || Frame > MAX_int32 || Frame != FMath::FloorToDouble(Frame))
            return Failure(TEXT("RefFrameIndex must be a nonnegative integer"));
        UAnimSequence* Reference = Sequence->RefPoseSeq;
        if (const auto* Value = (*Settings)->Values.Find(TEXT("RefPoseSeq")))
        {
            if ((*Value)->IsNull()) Reference = nullptr;
            else
            {
                FString Path; if (!(*Value)->TryGetString(Path)) return Failure(TEXT("RefPoseSeq must be null or an exact /Game Sequence package path"));
                auto RefParams = MakeShared<FJsonObject>(); RefParams->SetStringField(TEXT("asset_path"), Path);
                Reference = Cast<UAnimSequence>(Load(RefParams, Error));
                if (!Reference) return Failure(TEXT("Invalid reference Sequence: ") + Error);
            }
        }
        const bool NeedsReference = Pose == ABPT_AnimFrame || Pose == ABPT_AnimScaled;
        if (Additive != AAT_None && Pose == ABPT_None) return Failure(TEXT("Additive animation requires an explicit reference pose strategy"));
        if (Reference && (!NeedsReference || Additive == AAT_None)) return Failure(TEXT("This strategy clears RefPoseSeq in UE; explicitly supply null before editing"));
        if (Additive != AAT_None && NeedsReference && (!Reference || Reference == Sequence || Reference->GetSkeleton() != Sequence->GetSkeleton()
            || Reference->GetPackage()->IsDirty() || Reference->AdditiveAnimType != AAT_None))
            return Failure(TEXT("Require a saved non-additive reference Sequence with the exact same Skeleton; self/cyclic/dirty/compiling/incompatible references refused"));
        const auto* FrameSource = Pose == ABPT_AnimFrame ? Reference : Sequence;
        if (Additive != AAT_None && (Pose == ABPT_AnimFrame || Pose == ABPT_LocalAnimFrame)
            && (!FrameSource || Frame >= FrameSource->GetNumberOfSampledKeys())) return Failure(TEXT("Reference frame outside actual sampled keys; no implicit clamping"));
        NewSettings->SetField(TEXT("RefPoseSeq"), FReader().Reference(Reference));
        auto Proposed = MakeShared<FJsonObject>(*Before); Proposed->SetObjectField(TEXT("settings"), NewSettings); Seal(Proposed);
        const bool Changed = Proposed->GetStringField(TEXT("baseline")) != Before->GetStringField(TEXT("baseline"));
        auto Result = Receipt(Before, DryRun, Changed); Result->SetObjectField(TEXT("proposed"), Proposed);
        if (DryRun || !Changed) return Result;
        TArray<FProperty*> ChangedProperties;
        auto ChangedProperty = [&ChangedProperties, Sequence](const TCHAR* Name, bool ChangedField)
        { if (ChangedField) ChangedProperties.Add(Sequence->GetClass()->FindPropertyByName(FName(Name))); };
        ChangedProperty(TEXT("AdditiveAnimType"), Sequence->AdditiveAnimType != Additive);
        ChangedProperty(TEXT("RefPoseType"), Sequence->RefPoseType != Pose);
        ChangedProperty(TEXT("RefPoseSeq"), Sequence->RefPoseSeq != Reference);
        ChangedProperty(TEXT("RefFrameIndex"), Sequence->RefFrameIndex != static_cast<int32>(Frame));
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "AnimationAdditive", "Set animation additive settings"));
            Sequence->SetFlags(RF_Transactional); Sequence->Modify();
            Sequence->AdditiveAnimType = static_cast<EAdditiveAnimationType>(Additive); Sequence->RefPoseType = static_cast<EAdditiveBasePoseType>(Pose);
            Sequence->RefPoseSeq = Reference; Sequence->RefFrameIndex = static_cast<int32>(Frame);
            for (auto* Property : ChangedProperties) { FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet); Sequence->PostEditChangeProperty(Event); }
            Sequence->MarkPackageDirty();
        }
        Result->SetBoolField(TEXT("modified"), true);
        TArray<UAnimSequence*> Targets{Sequence}; UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
        auto After = Snapshot(Sequence);
        if (!After->GetBoolField(TEXT("complete")) || After->GetStringField(TEXT("baseline")) != Proposed->GetStringField(TEXT("baseline")))
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback"));
            Result->SetStringField(TEXT("error"), TEXT("Derived data/readback differs from validated proposal; changes remain unsaved"));
        }
        Result->SetBoolField(TEXT("target_compilation_complete"), true); Finish(Sequence, Result, SaveAsset); return Result;
    }

    TSharedPtr<FJsonObject> AddNotify(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; TSharedPtr<FJsonObject> Before;
        auto* Montage = Cast<UAnimMontage>(Prepare(Params, {TEXT("asset_path"), TEXT("expected_baseline"), TEXT("notify_class"), TEXT("event_guid"),
            TEXT("time_seconds"), TEXT("track_index"), TEXT("track_name"), TEXT("tick_type"), TEXT("event_settings"), TEXT("dry_run"), TEXT("save")}, Before, Error));
        if (!Montage) return Failure(Error.IsEmpty() ? TEXT("Native notify addition requires AnimMontage") : Error);
        bool DryRun, SaveAsset; if (!Flags(Params, DryRun, SaveAsset, Error)) return Failure(Error);
        FString ClassPath, GuidText, TrackName, Tick;
        double Time, TrackIndex;
        if (!Montage->GetSkeleton() || !Params->TryGetStringField(TEXT("notify_class"), ClassPath) || !ClassPath.StartsWith(TEXT("/Script/")) || ClassPath.Len() >= NAME_SIZE
            || !Params->TryGetStringField(TEXT("event_guid"), GuidText) || !Params->TryGetStringField(TEXT("track_name"), TrackName)
            || TrackName.IsEmpty() || TrackName.Len() >= NAME_SIZE || !Params->TryGetStringField(TEXT("tick_type"), Tick)
            || !Params->TryGetNumberField(TEXT("time_seconds"), Time) || !FMath::IsFinite(Time) || Time < 0 || Time >= Montage->GetPlayLength()
            || !Params->TryGetNumberField(TEXT("track_index"), TrackIndex) || !FMath::IsFinite(TrackIndex) || TrackIndex < 0 || TrackIndex > MAX_int32 || TrackIndex != FMath::FloorToDouble(TrackIndex))
            return Failure(TEXT("Require valid Skeleton, registered /Script notify_class, event_guid, exact track index/name, tick_type and finite time in [0,length)"));
        if (!FMath::IsFinite(static_cast<float>(Time)) || static_cast<float>(Time) >= Montage->GetPlayLength()) return Failure(TEXT("Time rounds outside native float timeline range"));
        FGuid Guid;
        if (!FGuid::ParseExact(GuidText, EGuidFormats::DigitsWithHyphens, Guid) || !Guid.IsValid()) return Failure(TEXT("event_guid requires a nonzero hyphenated GUID; reuse it to identify this author event"));
        if (!Montage->AnimNotifyTracks.IsValidIndex(static_cast<int32>(TrackIndex)) || Montage->AnimNotifyTracks[static_cast<int32>(TrackIndex)].TrackName.ToString() != TrackName)
            return Failure(TEXT("Exact native track index/name mismatch; no automatic track creation"));
        int32 TrackMatches = 0;
        for (const auto& Track : Montage->AnimNotifyTracks) if (Track.TrackName.ToString() == TrackName) ++TrackMatches;
        if (TrackMatches != 1) return Failure(TEXT("Ambiguous duplicate track names refused"));
        auto* Class = FindObject<UClass>(nullptr, *ClassPath);
        if (!Class || Class->GetPathName() != ClassPath || !Class->IsChildOf(UAnimNotify::StaticClass()) || !Class->HasAnyClassFlags(CLASS_Native)
            || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_CompiledFromBlueprint)
            || Class->GetMetaData(TEXT("MCPAnimationNotifyAuthoringSafe")) != TEXT("true"))
            return Failure(TEXT("Class must already be registered, native/nonabstract and explicitly opt in with MCPAnimationNotifyAuthoringSafe=true; no Blueprint/class loading"));
        for (TFieldIterator<FProperty> Field(Class); Field && Field->GetOwnerStruct() != UAnimNotify::StaticClass(); ++Field)
            if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) return Failure(TEXT("This first contract supports no additional saved native Notify fields; use a dedicated typed extension"));
        auto* Defaults = Cast<UAnimNotify>(Class->GetDefaultObject(false));
        if (!Defaults || !Defaults->CanBePlaced(Montage)) return Failure(TEXT("Require an existing native CDO that can be placed on this Montage"));
        if (Tick != TEXT("Queued") && Tick != TEXT("BranchingPoint")) return Failure(TEXT("tick_type must be Queued or BranchingPoint"));
        if (Defaults->bIsNativeBranchingPoint && Tick != TEXT("BranchingPoint")) return Failure(TEXT("Native class forces BranchingPoint; cannot promise Queued behavior"));
        TSet<FGuid> Existing;
        for (const auto& Event : Montage->Notifies)
        {
            if (!Event.Guid.IsValid() || Existing.Contains(Event.Guid)) return Failure(TEXT("Existing event GUIDs missing/ambiguous; cannot preserve author identity safely"));
            Existing.Add(Event.Guid);
        }
        if (Existing.Contains(Guid)) return Failure(TEXT("event_guid already exists; explicitly refused, not replayed or deduplicated by class/time"));
        FAnimNotifyEvent Planned; Planned.Guid = Guid; Planned.Notify = Defaults; Planned.NotifyName = FName(*Defaults->GetNotifyName());
        Planned.TrackIndex = static_cast<int32>(TrackIndex); Planned.Link(Montage, static_cast<float>(Time));
        Planned.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(static_cast<float>(Time)));
        Planned.MontageTickType = Tick == TEXT("BranchingPoint") ? EMontageNotifyTickType::BranchingPoint : EMontageNotifyTickType::Queued;
        const TSharedPtr<FJsonObject>* Settings = nullptr;
        const TSet<FString> Allowed{TEXT("TriggerWeightThreshold"), TEXT("NotifyTriggerChance"), TEXT("NotifyFilterType"), TEXT("NotifyFilterLOD"),
            TEXT("bCanBeFilteredViaRequest"), TEXT("bTriggerOnDedicatedServer"), TEXT("bTriggerOnFollower")};
        if (Params->HasField(TEXT("event_settings")))
        {
            if (!Params->TryGetObjectField(TEXT("event_settings"), Settings)) return Failure(TEXT("event_settings must be an object"));
            for (const auto& Pair : (*Settings)->Values)
            {
                if (!Allowed.Contains(Pair.Key)) return Failure(TEXT("Unsupported event setting: ") + Pair.Key);
                auto* Property = FAnimNotifyEvent::StaticStruct()->FindPropertyByName(FName(*Pair.Key));
                if (!Property || !UnrealMCPAssetProperties::DecodeValue(Property, Property->ContainerPtrToValuePtr<void>(&Planned), Pair.Value, Error)) return Failure(Pair.Key + TEXT(": ") + Error);
            }
        }
        if (!FMath::IsFinite(Planned.NotifyTriggerChance) || Planned.NotifyTriggerChance < 0 || Planned.NotifyTriggerChance > 1
            || !FMath::IsFinite(Planned.TriggerWeightThreshold) || Planned.TriggerWeightThreshold < 0 || Planned.TriggerWeightThreshold > 1 || Planned.NotifyFilterLOD < 0)
            return Failure(TEXT("Invalid trigger probability/weight/LOD"));
        auto Sorted = Montage->Notifies; Sorted.Add(Planned); Sorted.Sort();
        TArray<FGuid> OriginalOrder, NewOldOrder;
        for (const auto& Event : Montage->Notifies) OriginalOrder.Add(Event.Guid);
        for (const auto& Event : Sorted) if (Event.Guid != Guid) NewOldOrder.Add(Event.Guid);
        if (OriginalOrder != NewOldOrder) return Failure(TEXT("Native sorting would reorder old events; refuse instead of normalizing existing author data"));
        FReader Reader; Reader.Root = Montage;
        auto PlannedFields = Reader.Fields(FAnimNotifyEvent::StaticStruct(), &Planned, TEXT("new_event"), 0);
        auto DefaultFields = Reader.Object(Defaults, TEXT("class_defaults"), 0);
        if (Reader.Failures) return Failure(TEXT("Native defaults/event cannot be read completely"));
        PlannedFields->RemoveField(TEXT("Notify"));
        auto Proposal = MakeShared<FJsonObject>(); Proposal->SetStringField(TEXT("event_guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
        Proposal->SetStringField(TEXT("notify_class"), ClassPath); Proposal->SetNumberField(TEXT("native_time_seconds"), static_cast<float>(Time));
        Proposal->SetObjectField(TEXT("event_fields"), PlannedFields); Proposal->SetField(TEXT("notify_class_defaults"), DefaultFields);
        Proposal->SetStringField(TEXT("object_path"), TEXT("Assigned by UE only on apply; class_defaults are not a new instance"));
        auto Result = Receipt(Before, DryRun, true); Result->SetObjectField(TEXT("proposed"), Proposal); Result->SetStringField(TEXT("event_guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
        if (DryRun) return Result;
        UAnimNotify* Created = nullptr;
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "MontageNativeNotify", "Add montage native notify"));
            Montage->SetFlags(RF_Transactional); Montage->Modify();
            Created = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, FName(*TrackName), static_cast<float>(Time), Class);
            auto* Event = Montage->Notifies.FindByPredicate([Created](const FAnimNotifyEvent& Item) { return Created && Item.Notify == Created; });
            if (Event)
            {
                Event->Guid = Guid; Event->MontageTickType = Planned.MontageTickType;
                for (const auto& Name : Allowed)
                {
                    auto* Property = FAnimNotifyEvent::StaticStruct()->FindPropertyByName(FName(*Name));
                    Property->CopyCompleteValue_InContainer(Event, &Planned);
                }
                static_cast<UAnimSequenceBase*>(Montage)->RefreshCacheData();
            }
            Montage->MarkPackageDirty();
        }
        Result->SetBoolField(TEXT("modified"), true);
        auto After = Snapshot(Montage); bool Preserved = After->GetBoolField(TEXT("complete"));
        auto BeforeScope = MakeShared<FJsonObject>(*Before); auto AfterScope = MakeShared<FJsonObject>(*After);
        for (const auto* Name : {TEXT("notifies"), TEXT("notify_count"), TEXT("baseline"), TEXT("package_dirty"), TEXT("semantics")}) { BeforeScope->RemoveField(Name); AfterScope->RemoveField(Name); }
        Preserved &= Json(BeforeScope) == Json(AfterScope);
        TArray<FString> ActualOldOrder;
        for (const auto& RowValue : After->GetArrayField(TEXT("notifies")))
        {
            const auto Row = RowValue->AsObject();
            if (Row->GetStringField(TEXT("guid")) == Guid.ToString(EGuidFormats::DigitsWithHyphens)) continue;
            ActualOldOrder.Add(Row->GetStringField(TEXT("guid")));
            const auto* Old = Before->GetArrayField(TEXT("notifies")).FindByPredicate([&Row](const TSharedPtr<FJsonValue>& Item) { return Item->AsObject()->GetStringField(TEXT("guid")) == Row->GetStringField(TEXT("guid")); });
            if (!Old) { Preserved = false; continue; }
            auto Left = MakeShared<FJsonObject>(*(*Old)->AsObject()); auto Right = MakeShared<FJsonObject>(*Row); Left->RemoveField(TEXT("index")); Right->RemoveField(TEXT("index"));
            Preserved &= Json(Left) == Json(Right);
        }
        TArray<FString> ExpectedOldOrder; for (const auto& OldGuid : OriginalOrder) ExpectedOldOrder.Add(OldGuid.ToString(EGuidFormats::DigitsWithHyphens));
        const auto* Added = Montage->Notifies.FindByPredicate([&Guid](const FAnimNotifyEvent& Item) { return Item.Guid == Guid; });
        FReader ActualReader; ActualReader.Root = Montage;
        auto ActualFields = Added ? ActualReader.Fields(FAnimNotifyEvent::StaticStruct(), Added, TEXT("new_event"), 0) : MakeShared<FJsonObject>(); ActualFields->RemoveField(TEXT("Notify"));
        Preserved &= ActualOldOrder == ExpectedOldOrder && Created && Created->GetOuter() == Montage && Created->GetClass() == Class
            && Added && Added->Notify == Created && Montage->Notifies.Num() == OriginalOrder.Num() + 1 && Json(ActualFields) == Json(PlannedFields);
        if (!Preserved)
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback"));
            Result->SetStringField(TEXT("error"), TEXT("Actual stored event or preserved author data differs; applied state remains unsaved, never retry blindly"));
        }
        Result->SetBoolField(TEXT("existing_data_preserved"), Preserved); Finish(Montage, Result, SaveAsset); return Result;
    }
}

void UMCPAnimationAppearanceTestNotify::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
    ++CallCount;
    auto* Component = NewObject<UNiagaraComponent>();
    Component->SetAsset(Template);
    Component->SetVariableFloat(Appearance.SizeParameter, Appearance.SizeMultiplier);
    bool Valid = false;
    DeliveredSize = Component->GetVariableFloat(Appearance.SizeParameter, Valid);
    if (!Valid) DeliveredSize = -1;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Commands/UnrealMCPNiagaraCommands.h"
#include "IAssetCompilingManager.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundWave.h"
#include "Modules/ModuleManager.h"

namespace UnrealMCPAnimation
{
    namespace
    {
        UObject* Fixture(UClass* Class, const FString& Root, const FString& Name)
        {
            auto* Package = CreatePackage(*(Root + TEXT("/") + Name));
            auto* Asset = NewObject<UObject>(Package, Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Asset); Asset->MarkPackageDirty(); return Asset;
        }

        UAnimSequence* FixtureSequence(const FString& Root)
        {
            auto* Skeleton = Cast<USkeleton>(Fixture(USkeleton::StaticClass(), Root, TEXT("SKEL_Test")));
            auto* Mesh = NewObject<USkeletalMesh>();
            {
                FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), nullptr);
                Modifier.Add(FMeshBoneInfo(FName(TEXT("root")), TEXT("root"), INDEX_NONE), FTransform::Identity);
            }
            Skeleton->MergeAllBonesToBoneTree(Mesh); Skeleton->RegisterSlotNode(FName(TEXT("DefaultSlot")));
            auto* Sequence = Cast<UAnimSequence>(Fixture(UAnimSequence::StaticClass(), Root, TEXT("A_Test"))); Sequence->SetSkeleton(Skeleton);
            auto& Controller = Sequence->GetController(); Controller.InitializeModel(); Controller.OpenBracket(FText::FromString(TEXT("Animation fixture")), false);
            Controller.SetFrameRate(FFrameRate(30, 1), false); Controller.SetNumberOfFrames(FFrameNumber(2), false); Controller.AddBoneCurve(FName(TEXT("root")), false);
            Controller.SetBoneTrackKeys(FName(TEXT("root")), TArray<FVector3f>{{2, 0, 0}, {6, 0, 0}, {10, 0, 0}},
                TArray<FQuat4f>{FQuat4f::Identity, FQuat4f::Identity, FQuat4f::Identity}, TArray<FVector3f>{FVector3f::OneVector, FVector3f::OneVector, FVector3f::OneVector}, false);
            Controller.NotifyPopulated(); Controller.CloseBracket(false);
            TArray<UAnimSequence*> Targets{Sequence}; UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
            return Sequence;
        }

        double SampleRoot(UAnimSequence* Sequence, double Time)
        {
            FMemMark Mark(FMemStack::Get());
            FBoneContainer Bones({FBoneIndexType(0)}, UE::Anim::FCurveFilterSettings(), *Sequence->GetSkeleton());
            FCompactPose Pose; Pose.SetBoneContainer(&Bones); FBlendedCurve Curve; Curve.InitFrom(Bones);
            UE::Anim::FStackAttributeContainer Attributes; FAnimationPoseData Data(Pose, Curve, Attributes);
            Sequence->GetAnimationPose(Data, FAnimExtractContext(Time)); return Pose[FCompactPoseBoneIndex(0)].GetTranslation().X;
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationReadTest, "UnrealMCP.Animation.Read", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPAnimationReadTest::RunTest(const FString& Parameters)
{
    auto* Montage = NewObject<UAnimMontage>(); Montage->InitializeNotifyTrack();
    auto* Notify = NewObject<UAnimNotify_PlaySound>(Montage);
    auto& Event = Montage->Notifies.AddDefaulted_GetRef(); Event.Notify = Notify; Event.Guid = FGuid::NewGuid(); Event.TrackIndex = 0; Event.Link(Montage, 0);
    const bool Dirty = Montage->GetPackage()->IsDirty();
    const auto Before = UnrealMCPAnimation::Snapshot(Montage);
    if (!TestTrue(TEXT("Standard montage and sound notify completely readable"), Before->GetBoolField(TEXT("complete"))))
    { AddError(UnrealMCPAnimation::Json(Before)); return false; }
    TestEqual(TEXT("One actual row"), Before->GetNumberField(TEXT("notify_count")), 1.0);
    TestEqual(TEXT("Read preserves dirty"), Montage->GetPackage()->IsDirty(), Dirty);
    TestEqual(TEXT("Stable baseline"), UnrealMCPAnimation::Snapshot(Montage)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Notify->VolumeMultiplier = 0.25f;
    TestNotEqual(TEXT("Real notify field changes baseline"), UnrealMCPAnimation::Snapshot(Montage)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Montage->RateScale = std::numeric_limits<float>::infinity(); const auto Invalid = UnrealMCPAnimation::Snapshot(Montage);
    TestFalse(TEXT("Nonfinite stored data is incomplete"), Invalid->GetBoolField(TEXT("complete")));
    TestTrue(TEXT("Nonfinite data has no write baseline"), Invalid->Values[TEXT("baseline")]->IsNull());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationAdditiveTest, "UnrealMCP.Animation.Additive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPAnimationAdditiveTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPAnimation;
    const FString Root = TEXT("/Game/__Dev/Animation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Sequence = FixtureSequence(Root); auto* Skeleton = Sequence->GetSkeleton();
    TArray<UAnimSequence*> Targets{Sequence}; UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
    if (!TestTrue(TEXT("Skeleton fixture save"), UEditorAssetLibrary::SaveLoadedAsset(Skeleton, false))
        || !TestTrue(TEXT("Sequence fixture save"), UEditorAssetLibrary::SaveLoadedAsset(Sequence, false))) return false;
    TestTrue(TEXT("Absolute source sampling"), FMath::IsNearlyEqual(SampleRoot(Sequence, Sequence->GetPlayLength()), 10.0, 0.001));
    const FString File = FPackageName::LongPackageNameToFilename(Root + TEXT("/A_Test"), FPackageName::GetAssetPackageExtension());
    const auto DiskBefore = FMD5Hash::HashFile(*File);
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/A_Test"));
    auto Refresh = [&]() { const auto State = Read(Params); Params->SetStringField(TEXT("expected_baseline"), State->GetStringField(TEXT("baseline"))); return State; };
    const auto Before = Refresh(); auto Settings = MakeShared<FJsonObject>();
    Settings->SetStringField(TEXT("AdditiveAnimType"), TEXT("AAT_LocalSpaceBase")); Settings->SetStringField(TEXT("RefPoseType"), TEXT("ABPT_LocalAnimFrame"));
    Settings->SetField(TEXT("RefPoseSeq"), MakeShared<FJsonValueNull>()); Settings->SetNumberField(TEXT("RefFrameIndex"), 0); Params->SetObjectField(TEXT("settings"), Settings);
    const auto Preview = SetAdditive(Params);
    if (!TestTrue(TEXT("Valid Additive preview"), Preview->GetBoolField(TEXT("success")))) { AddError(Json(Preview)); return false; }
    TestFalse(TEXT("Preview not modified"), Preview->GetBoolField(TEXT("modified")));
    TestTrue(TEXT("Preview leaves file hash"), FMD5Hash::HashFile(*File) == DiskBefore);
    auto* Reference = DuplicateObject<UAnimSequence>(Sequence, CreatePackage(*(Root + TEXT("/A_Reference"))), FName(TEXT("A_Reference")));
    Reference->SetFlags(RF_Public | RF_Standalone | RF_Transactional); FAssetRegistryModule::AssetCreated(Reference);
    TArray<UAnimSequence*> ReferenceTargets{Reference}; UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(ReferenceTargets);
    if (!TestTrue(TEXT("Compatible reference saved"), UEditorAssetLibrary::SaveLoadedAsset(Reference, false))) return false;
    Settings->SetStringField(TEXT("RefPoseType"), TEXT("ABPT_AnimFrame")); Settings->SetStringField(TEXT("RefPoseSeq"), Root + TEXT("/A_Reference"));
    const auto ReferencePreview = SetAdditive(Params);
    if (!TestTrue(TEXT("Compatible external AnimFrame preview"), ReferencePreview->GetBoolField(TEXT("success")))) { AddError(Json(ReferencePreview)); return false; }
    Settings->SetStringField(TEXT("RefPoseType"), TEXT("ABPT_AnimScaled")); TestTrue(TEXT("Compatible AnimScaled preview"), SetAdditive(Params)->GetBoolField(TEXT("success")));
    Settings->SetStringField(TEXT("RefPoseType"), TEXT("ABPT_AnimFrame")); Settings->SetStringField(TEXT("RefPoseSeq"), Root + TEXT("/A_Test"));
    TestFalse(TEXT("Self reference refused"), SetAdditive(Params)->GetBoolField(TEXT("success")));
    auto* Wrong = FixtureSequence(Root + TEXT("/DifferentSkeleton"));
    if (!TestTrue(TEXT("Incompatible reference fixture saved"), UEditorAssetLibrary::SaveLoadedAsset(Wrong->GetSkeleton(), false) && UEditorAssetLibrary::SaveLoadedAsset(Wrong, false))) return false;
    Settings->SetStringField(TEXT("RefPoseSeq"), Wrong->GetPackage()->GetName()); TestFalse(TEXT("Different Skeleton refused"), SetAdditive(Params)->GetBoolField(TEXT("success")));
    Settings->SetStringField(TEXT("RefPoseType"), TEXT("ABPT_LocalAnimFrame")); Settings->SetField(TEXT("RefPoseSeq"), MakeShared<FJsonValueNull>());
    Skeleton->GetPackage()->SetDirtyFlag(true); TestFalse(TEXT("Dirty Skeleton dependency refused"), SetAdditive(Params)->GetBoolField(TEXT("success"))); Skeleton->GetPackage()->SetDirtyFlag(false);
    Settings->SetNumberField(TEXT("RefFrameIndex"), 9999);
    TestFalse(TEXT("Invalid frame refused"), SetAdditive(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Invalid frame leaves baseline"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Settings->SetNumberField(TEXT("RefFrameIndex"), 0); Params->SetBoolField(TEXT("dry_run"), false);
    const auto Draft = SetAdditive(Params);
    if (!TestTrue(TEXT("Additive draft succeeded"), Draft->GetBoolField(TEXT("success")))) { AddError(Json(Draft)); return false; }
    TestTrue(TEXT("Draft modified/dirty, not saved"), Draft->GetBoolField(TEXT("modified")) && Draft->GetBoolField(TEXT("package_dirty")) && !Draft->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("Actual additive pose is delta from confirmed frame0"), FMath::IsNearlyEqual(SampleRoot(Sequence, Sequence->GetPlayLength()), 8.0, 0.001));
    TestTrue(TEXT("Draft disk unchanged"), FMD5Hash::HashFile(*File) == DiskBefore);
    TestFalse(TEXT("Dirty target rejected"), SetAdditive(Params)->GetBoolField(TEXT("success")));
    GEditor->UndoTransaction(); UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
    TestEqual(TEXT("Undo original mode"), Sequence->AdditiveAnimType.GetValue(), EAdditiveAnimationType::AAT_None);
    GEditor->RedoTransaction(); UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
    TestEqual(TEXT("Redo additive mode"), Sequence->AdditiveAnimType.GetValue(), EAdditiveAnimationType::AAT_LocalSpaceBase);
    GEditor->UndoTransaction(); UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
    Sequence->GetPackage()->SetDirtyFlag(false); Refresh(); Params->SetBoolField(TEXT("save"), true);
    FailSaveForTest = true; const auto Failed = SetAdditive(Params); FailSaveForTest = false;
    TestTrue(TEXT("Save failure reports actual applied change"), Failed->GetBoolField(TEXT("modified")) && !Failed->GetBoolField(TEXT("success")) && !Failed->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Save failure stage"), Failed->GetStringField(TEXT("stage")), FString(TEXT("save")));
    TestTrue(TEXT("Injected save failure preserves disk"), FMD5Hash::HashFile(*File) == DiskBefore);
    GEditor->UndoTransaction(); UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets); Sequence->GetPackage()->SetDirtyFlag(false); Refresh();
    const auto Applied = SetAdditive(Params);
    if (!TestTrue(TEXT("Additive saved"), Applied->GetBoolField(TEXT("success")) && Applied->GetBoolField(TEXT("saved")))) { AddError(Json(Applied)); return false; }
    Refresh(); const auto Repeat = SetAdditive(Params); TestTrue(TEXT("Same Additive settings no-op"), Repeat->GetBoolField(TEXT("success")) && !Repeat->GetBoolField(TEXT("modified")) && !Repeat->GetBoolField(TEXT("saved")));
    Params->SetStringField(TEXT("expected_baseline"), TEXT("stale")); TestFalse(TEXT("Stale Additive rejected"), SetAdditive(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Source data unchanged"), Snapshot(Sequence)->GetStringField(TEXT("source_data_guid")), Before->GetStringField(TEXT("source_data_guid")));
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("AnimationAcceptance") / FPackageName::GetShortName(Root) / TEXT("sequence.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt), true); TestTrue(TEXT("Cold oracle saved"), FFileHelper::SaveStringToFile(Json(Read(Params)), *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("Animation fixture: ") + Root); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationNotifyTest, "UnrealMCP.Animation.Notify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPAnimationNotifyTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPAnimation;
    const FString Root = TEXT("/Game/__Dev/AnimationNotify_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Sequence = FixtureSequence(Root); auto* Skeleton = Sequence->GetSkeleton();
    if (!TestTrue(TEXT("Notify rig saved"), UEditorAssetLibrary::SaveLoadedAsset(Skeleton, false) && UEditorAssetLibrary::SaveLoadedAsset(Sequence, false))) return false;
    auto* Dynamic = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, FName(TEXT("DefaultSlot")), 0.03f, 0.04f);
    auto* Montage = DuplicateObject<UAnimMontage>(Dynamic, CreatePackage(*(Root + TEXT("/AM_Test"))), FName(TEXT("AM_Test")));
    Montage->ClearFlags(RF_Transient); Montage->SetFlags(RF_Public | RF_Standalone | RF_Transactional); FAssetRegistryModule::AssetCreated(Montage); Montage->InitializeNotifyTrack();
    static_cast<UAnimCompositeBase*>(Montage)->SetCompositeLength(Sequence->GetPlayLength());
    if (!TestTrue(TEXT("Saved montage fixture has actual timeline length"), Montage->GetPlayLength() > 0.05f)) return false;
    auto* Sound = Cast<UAnimNotify_PlaySound>(UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, Montage->AnimNotifyTracks[0].TrackName, 0.01f, UAnimNotify_PlaySound::StaticClass()));
    if (!TestNotNull(TEXT("Existing Sound notify created"), Sound)) return false;
    Sound->VolumeMultiplier = 0.23f; Sound->PitchMultiplier = 0.75f; Sound->AttachName = FName(TEXT("muzzle"));
    Sound->Sound = Cast<USoundBase>(UEditorAssetLibrary::LoadAsset(TEXT("/Engine/EditorSounds/Notifications/CompileSuccess")));
    if (!TestNotNull(TEXT("Valid existing Sound reference"), Sound->Sound.Get())) return false;
    FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("NiagaraAnimNotifies"));
    auto* NiagaraClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraAnimNotifies.AnimNotify_PlayNiagaraEffect"));
    if (!TestNotNull(TEXT("Standard Niagara notify class"), NiagaraClass)) return false;
    auto* Niagara = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, Montage->AnimNotifyTracks[0].TrackName, 0.05f, NiagaraClass);
    auto* Scale = FindFProperty<FStructProperty>(NiagaraClass, TEXT("Scale")); auto* Socket = FindFProperty<FNameProperty>(NiagaraClass, TEXT("SocketName"));
    if (!TestNotNull(TEXT("Niagara scale property"), Scale) || !TestNotNull(TEXT("Niagara socket property"), Socket)) return false;
    *Scale->ContainerPtrToValuePtr<FVector>(Niagara) = FVector(2, 3, 4); Socket->SetPropertyValue_InContainer(Niagara, FName(TEXT("muzzle")));
    Montage->MarkPackageDirty(); if (!TestTrue(TEXT("Existing Sound/Niagara montage saved"), UEditorAssetLibrary::SaveLoadedAsset(Montage, false))) return false;
    TArray<UObject*> ReadyTargets{Sequence, Montage, Sound->Sound.Get()}; FAssetCompilingManager::Get().FinishCompilationForObjects(ReadyTargets);
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/AM_Test"));
    auto Refresh = [&]() { const auto State = Read(Params); Params->SetStringField(TEXT("expected_baseline"), State->GetStringField(TEXT("baseline"))); return State; };
    const auto Before = Refresh(); if (!TestTrue(TEXT("Standard Sound/Niagara completely read"), Before->GetBoolField(TEXT("complete")))) { AddError(Json(Before)); return false; }
    const auto Guid = FGuid::NewGuid(); Params->SetStringField(TEXT("event_guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("notify_class"), UMCPAnimationTestNotify::StaticClass()->GetPathName()); Params->SetNumberField(TEXT("time_seconds"), 0.02);
    Params->SetNumberField(TEXT("track_index"), 0); Params->SetStringField(TEXT("track_name"), Montage->AnimNotifyTracks[0].TrackName.ToString()); Params->SetStringField(TEXT("tick_type"), TEXT("BranchingPoint"));
    const FString File = FPackageName::LongPackageNameToFilename(Root + TEXT("/AM_Test"), FPackageName::GetAssetPackageExtension()); const auto HashBefore = FMD5Hash::HashFile(*File);
    UMCPAnimationTestNotify::CallCount = 0;
    const auto Preview = AddNotify(Params);
    if (!TestTrue(TEXT("Native Notify preview accepted"), Preview->GetBoolField(TEXT("success")))) { AddError(Json(Preview)); return false; }
    TestFalse(TEXT("Preview no mutation"), Preview->GetBoolField(TEXT("modified")));
    TestEqual(TEXT("Preview does not execute native gameplay callback"), UMCPAnimationTestNotify::CallCount, 0);
    TestEqual(TEXT("Preview actual count stays two"), Montage->Notifies.Num(), 2);
    Params->SetStringField(TEXT("tick_type"), TEXT("Queued")); TestFalse(TEXT("Native branching cannot be forced queued"), AddNotify(Params)->GetBoolField(TEXT("success"))); Params->SetStringField(TEXT("tick_type"), TEXT("BranchingPoint"));
    Params->SetStringField(TEXT("notify_class"), TEXT("/Script/NodeFall.NotRegisteredCannonNotify")); TestFalse(TEXT("Unknown project class refused"), AddNotify(Params)->GetBoolField(TEXT("success")));
    Params->SetStringField(TEXT("notify_class"), UAnimNotify::StaticClass()->GetPathName()); TestFalse(TEXT("Abstract/non-opted-in class refused"), AddNotify(Params)->GetBoolField(TEXT("success")));
    Params->SetStringField(TEXT("notify_class"), UMCPAnimationTestNotify::StaticClass()->GetPathName());
    Params->SetNumberField(TEXT("track_index"), 999); TestFalse(TEXT("Invalid track refused"), AddNotify(Params)->GetBoolField(TEXT("success"))); Params->SetNumberField(TEXT("track_index"), 0);
    Params->SetNumberField(TEXT("time_seconds"), -1); TestFalse(TEXT("Invalid time refused"), AddNotify(Params)->GetBoolField(TEXT("success"))); Params->SetNumberField(TEXT("time_seconds"), 0.02);
    TestEqual(TEXT("All rejected previews leave baseline"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("dry_run"), false); const auto Draft = AddNotify(Params);
    if (!TestTrue(TEXT("Native Notify draft applied"), Draft->GetBoolField(TEXT("success")))) { AddError(Json(Draft)); return false; }
    TestTrue(TEXT("Draft preserves exact old data"), Draft->GetBoolField(TEXT("existing_data_preserved")));
    TestTrue(TEXT("Draft does not save disk"), FMD5Hash::HashFile(*File) == HashBefore);
    TestFalse(TEXT("Dirty notify target refused"), AddNotify(Params)->GetBoolField(TEXT("success")));
    GEditor->UndoTransaction(); TestEqual(TEXT("Notify undo two rows"), Montage->Notifies.Num(), 2);
    GEditor->RedoTransaction(); TestEqual(TEXT("Notify redo three rows"), Montage->Notifies.Num(), 3);
    GEditor->UndoTransaction(); Montage->GetPackage()->SetDirtyFlag(false); Refresh(); Params->SetBoolField(TEXT("save"), true);
    FailSaveForTest = true; const auto Failed = AddNotify(Params); FailSaveForTest = false;
    TestTrue(TEXT("Notify save failure actual flags"), !Failed->GetBoolField(TEXT("success")) && Failed->GetBoolField(TEXT("modified")) && !Failed->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("Notify save failure leaves disk"), FMD5Hash::HashFile(*File) == HashBefore);
    GEditor->UndoTransaction(); Montage->GetPackage()->SetDirtyFlag(false); Refresh(); const auto Applied = AddNotify(Params);
    if (!TestTrue(TEXT("Notify target saved"), Applied->GetBoolField(TEXT("success")) && Applied->GetBoolField(TEXT("saved")))) { AddError(Json(Applied)); return false; }
    Refresh(); const auto Duplicate = AddNotify(Params); TestFalse(TEXT("Same event GUID explicitly refused"), Duplicate->GetBoolField(TEXT("success"))); TestFalse(TEXT("No duplicate mutation"), Duplicate->GetBoolField(TEXT("modified")));
    TestEqual(TEXT("Exactly one native marker"), Montage->Notifies.Num(), 3);
    TestEqual(TEXT("Original Sound object retained"), Montage->Notifies[0].Notify.Get(), static_cast<UAnimNotify*>(Sound));
    TestEqual(TEXT("Sound settings retained"), Sound->VolumeMultiplier, 0.23f);
    auto* Component = NewObject<USkeletalMeshComponent>(); auto* AnimInstance = NewObject<UAnimInstance>(Component);
    UMCPAnimationTestNotify::CallCount = 0; FAnimMontageInstance Instance(AnimInstance); Instance.Initialize(Montage); Instance.Play(); Instance.UpdateWeight(1.0f); Instance.Advance(0.04f, nullptr, false);
    TestEqual(TEXT("Native montage advancement dispatched exactly once"), UMCPAnimationTestNotify::CallCount, 1);
    TestEqual(TEXT("Branching payload instance identity"), UMCPAnimationTestNotify::LastMontageInstance, Instance.GetInstanceID());
    TestEqual(TEXT("Branching payload source"), UMCPAnimationTestNotify::LastAnimation, static_cast<const UAnimSequenceBase*>(Montage));
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("AnimationAcceptance") / FPackageName::GetShortName(Root) / TEXT("montage.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt), true); TestTrue(TEXT("Notify cold oracle saved"), FFileHelper::SaveStringToFile(Json(Read(Params)), *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("Animation notify fixture: ") + Root); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationNotifyPropertiesTest, "UnrealMCP.Animation.NotifyProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPAnimationNotifyPropertiesTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPAnimation;
    const FString Root = TEXT("/Game/__Dev/NotifyProperties_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Sequence = FixtureSequence(Root);
    if (!UEditorAssetLibrary::SaveLoadedAsset(Sequence->GetSkeleton(), false) || !UEditorAssetLibrary::SaveLoadedAsset(Sequence, false)) return false;
    auto* Dynamic = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, FName(TEXT("DefaultSlot")), 0.03f, 0.04f);
    auto* Montage = DuplicateObject<UAnimMontage>(Dynamic, CreatePackage(*(Root + TEXT("/AM_Test"))), FName(TEXT("AM_Test")));
    Montage->ClearFlags(RF_Transient); Montage->SetFlags(RF_Public | RF_Standalone | RF_Transactional); FAssetRegistryModule::AssetCreated(Montage); Montage->InitializeNotifyTrack();
    static_cast<UAnimCompositeBase*>(Montage)->SetCompositeLength(Sequence->GetPlayLength());
    Montage->bEnableAutoBlendOut = false;
    const auto Track = Montage->AnimNotifyTracks[0].TrackName;
    auto* Sound = Cast<UAnimNotify_PlaySound>(UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, Track, 0.01f, UAnimNotify_PlaySound::StaticClass()));
    Sound->VolumeMultiplier = 0.23f;
    auto* Fire = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, Track, 0.02f, UMCPAnimationTestNotify::StaticClass());
    auto* Notify = Cast<UMCPAnimationAppearanceTestNotify>(UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, Track, 0.03f, UMCPAnimationAppearanceTestNotify::StaticClass()));
    Notify->Appearance.Colors.Add(FLinearColor(0.1f, 0.2f, 0.3f, 0.4f)); Notify->LocationOffset = FVector(2, 3, 4);
    FUnrealMCPNiagaraCommands Niagara;
    auto SystemParams = MakeShared<FJsonObject>(); SystemParams->SetStringField(TEXT("asset_path"), Root + TEXT("/NS_Test"));
    Niagara.HandleCommand(TEXT("create_niagara_system"), SystemParams);
    auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/NS_Test")));
    if (!TestNotNull(TEXT("Factory-initialized Niagara system"), System)) return false;
    System->GetExposedParameters().SetParameterValue(1.0f, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Size")), true);
    Notify->Template = System;
    if (!TestTrue(TEXT("Save isolated system and Montage"), UEditorAssetLibrary::SaveLoadedAsset(System, false) && UEditorAssetLibrary::SaveLoadedAsset(Montage, false))) return false;
    TArray<UObject*> Ready{Sequence, Montage, System}; FAssetCompilingManager::Get().FinishCompilationForObjects(Ready);
    System->WaitForCompilationComplete(true, false);
    FAssetCompilingManager::Get().FinishAllCompilation();
    FAssetCompilingManager::Get().ProcessAsyncTasks(false);
    for (const auto* Manager : FAssetCompilingManager::Get().GetRegisteredManagers())
        if (Manager->GetNumRemainingAssets() != 0)
            AddInfo(FString::Printf(TEXT("Notify fixture pending compiler: %s=%d"), *Manager->GetAssetTypeName().ToString(), Manager->GetNumRemainingAssets()));
    if (!TestEqual(TEXT("Fixture compilers are idle before reader/writer"), FAssetCompilingManager::Get().GetNumRemainingAssets(), 0)) return false;
    const auto* TargetEvent = Montage->Notifies.FindByPredicate([&](const FAnimNotifyEvent& Event) { return Event.Notify == Notify; });
    const FGuid Guid = TargetEvent->Guid;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/AM_Test")); Params->SetStringField(TEXT("event_guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
    auto ReaderParams = MakeShared<FJsonObject>(*Params);
    const int32 CompilingBeforeRead = FAssetCompilingManager::Get().GetNumRemainingAssets();
    const auto Legacy = Snapshot(Montage); TestFalse(TEXT("Legacy custom Notify remains partial"), Legacy->GetBoolField(TEXT("complete")));
    const auto Before = ReadNotifyProperties(ReaderParams);
    TestEqual(TEXT("Reader does not start compilation"), FAssetCompilingManager::Get().GetNumRemainingAssets(), CompilingBeforeRead);
    if (!TestTrue(TEXT("Dedicated finite Notify contract readable"), Before->GetBoolField(TEXT("success")))) { AddError(Json(Before)); return false; }
    auto Refresh = [&]()
    {
        const auto State = ReadNotifyProperties(ReaderParams);
        Params->SetStringField(TEXT("expected_baseline"), State->GetStringField(TEXT("baseline")));
        Params->SetStringField(TEXT("expected_class"), State->GetStringField(TEXT("notify_class")));
        Params->SetStringField(TEXT("expected_outer"), State->GetStringField(TEXT("outer_path"))); return State;
    };
    Refresh();
    auto Change = [](const TCHAR* Path, TSharedPtr<FJsonValue> Value)
    { auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("path"), Path); Result->SetField(TEXT("value"), Value); return MakeShared<FJsonValueObject>(Result); };
    TArray<TSharedPtr<FJsonValue>> Changes{
        Change(TEXT("Appearance.SizeParameter"), MakeShared<FJsonValueString>(TEXT("User.Size"))),
        Change(TEXT("Appearance.SizeMultiplier"), MakeShared<FJsonValueNumber>(0.1)),
        Change(TEXT("SocketName"), MakeShared<FJsonValueString>(TEXT("Muzzle"))),
        Change(TEXT("bAbsoluteScale"), MakeShared<FJsonValueBoolean>(true))};
    Params->SetArrayField(TEXT("changes"), Changes);
    const FString File = FPackageName::LongPackageNameToFilename(Root + TEXT("/AM_Test"), FPackageName::GetAssetPackageExtension()); const auto DiskBefore = FMD5Hash::HashFile(*File);
    const auto Preview = PatchNotifyProperties(Params);
    if (!TestTrue(TEXT("Preview accepted"), Preview->GetBoolField(TEXT("success")))) { AddError(Json(Preview)); return false; }
    TestFalse(TEXT("Preview clean"), Montage->GetPackage()->IsDirty()); TestEqual(TEXT("Preview no new Notify"), Montage->Notifies.Num(), 3);
    TestEqual(TEXT("Preview original multiplier"), Notify->Appearance.SizeMultiplier, 1.0f);
    auto Invalid = Changes; Invalid.Add(Change(TEXT("HiddenSentinel"), MakeShared<FJsonValueNumber>(9))); Params->SetArrayField(TEXT("changes"), Invalid);
    TestFalse(TEXT("Illegal final hidden member rejects batch"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success")));
    TestTrue(TEXT("Rejected batch has no mutation"), Notify->Appearance.SizeParameter.IsNone() && !Montage->GetPackage()->IsDirty());
    Invalid = Changes; Invalid.Add(Change(TEXT("Template"), MakeShared<FJsonValueString>(Sequence->GetPathName()))); Params->SetArrayField(TEXT("changes"), Invalid);
    TestFalse(TEXT("Wrong reference type refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success")));
    Invalid = Changes; Invalid[1] = Change(TEXT("Appearance.SizeMultiplier"), MakeShared<FJsonValueNumber>(std::numeric_limits<double>::infinity())); Params->SetArrayField(TEXT("changes"), Invalid);
    TestFalse(TEXT("Nonfinite value refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success")));
    Invalid = Changes; Invalid[0] = Change(TEXT("Appearance.SizeParameter"), MakeShared<FJsonValueString>(TEXT("User.Missing"))); Params->SetArrayField(TEXT("changes"), Invalid);
    TestFalse(TEXT("Missing User Float refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success"))); Params->SetArrayField(TEXT("changes"), Changes);
    Params->SetStringField(TEXT("expected_class"), UAnimNotify::StaticClass()->GetPathName()); TestFalse(TEXT("Wrong class refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success"))); Refresh();
    Params->SetStringField(TEXT("expected_outer"), TEXT("wrong")); TestFalse(TEXT("Wrong Outer refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success"))); Refresh();
    Params->SetStringField(TEXT("expected_baseline"), TEXT("stale")); TestFalse(TEXT("Stale refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success"))); Refresh();
    const auto OriginalGuid = Montage->Notifies[0].Guid; Montage->Notifies[0].Guid = Guid;
    TestFalse(TEXT("Duplicate GUID refused"), ReadNotifyProperties(ReaderParams)->GetBoolField(TEXT("success"))); Montage->Notifies[0].Guid = OriginalGuid;
    Notify->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    TestFalse(TEXT("Wrong real Outer refused"), ReadNotifyProperties(ReaderParams)->GetBoolField(TEXT("success"))); Notify->Rename(nullptr, Montage, REN_DontCreateRedirectors | REN_NonTransactional);
    Montage->GetPackage()->SetDirtyFlag(false); Refresh(); Params->SetBoolField(TEXT("dry_run"), false);
    const auto Draft = PatchNotifyProperties(Params);
    if (!TestTrue(TEXT("Apply draft preserves all protected data"), Draft->GetBoolField(TEXT("success")) && Draft->GetBoolField(TEXT("existing_data_preserved")))) { AddError(Json(Draft)); return false; }
    TestTrue(TEXT("Draft truthful"), Draft->GetBoolField(TEXT("modified")) && !Draft->GetBoolField(TEXT("saved")) && Draft->GetBoolField(TEXT("package_dirty")));
    TestTrue(TEXT("Draft disk unchanged"), FMD5Hash::HashFile(*File) == DiskBefore);
    TestFalse(TEXT("Dirty draft refused"), PatchNotifyProperties(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Stored size"), Notify->Appearance.SizeMultiplier, 0.1f); TestEqual(TEXT("Stored socket"), Notify->SocketName, FName(TEXT("Muzzle")));
    TestEqual(TEXT("Hidden sentinel retained"), Notify->HiddenSentinel, 73); TestEqual(TEXT("Brightness retained"), Notify->Appearance.BrightnessMultiplier, 3.0f);
    TestEqual(TEXT("Colors retained"), Notify->Appearance.Colors[0], FLinearColor(0.1f, 0.2f, 0.3f, 0.4f)); TestEqual(TEXT("Location retained"), Notify->LocationOffset, FVector(2, 3, 4));
    TestEqual(TEXT("Fire identity retained"), Montage->Notifies[1].Notify.Get(), Fire); TestEqual(TEXT("Sound identity retained"), Montage->Notifies[0].Notify.Get(), static_cast<UAnimNotify*>(Sound));
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo size"), Notify->Appearance.SizeMultiplier, 1.0f);
    GEditor->RedoTransaction(); TestEqual(TEXT("Redo size"), Notify->Appearance.SizeMultiplier, 0.1f);
    GEditor->UndoTransaction(); Montage->GetPackage()->SetDirtyFlag(false); Refresh(); Params->SetBoolField(TEXT("save"), true);
    FailSaveForTest = true; const auto Failed = PatchNotifyProperties(Params); FailSaveForTest = false;
    if (!TestTrue(TEXT("Save failure actual modified/unsaved"), !Failed->GetBoolField(TEXT("success")) && Failed->GetBoolField(TEXT("modified")) && !Failed->GetBoolField(TEXT("saved")))) { AddError(Json(Failed)); return false; }
    TestEqual(TEXT("Save failure stage"), Failed->GetStringField(TEXT("stage")), FString(TEXT("save"))); TestTrue(TEXT("Save failure disk unchanged"), FMD5Hash::HashFile(*File) == DiskBefore);
    GEditor->UndoTransaction(); Montage->GetPackage()->SetDirtyFlag(false); Refresh();
    const auto Applied = PatchNotifyProperties(Params);
    if (!TestTrue(TEXT("Apply/save and protected readback"), Applied->GetBoolField(TEXT("success")) && Applied->GetBoolField(TEXT("saved")))) { AddError(Json(Applied)); return false; }
    Refresh(); const auto Repeat = PatchNotifyProperties(Params); TestTrue(TEXT("Same values no-op/clean/no-save"), Repeat->GetBoolField(TEXT("success")) && !Repeat->GetBoolField(TEXT("modified")) && !Repeat->GetBoolField(TEXT("saved")) && !Repeat->GetBoolField(TEXT("package_dirty")));
    auto* Component = NewObject<USkeletalMeshComponent>(); auto* AnimInstance = NewObject<UAnimInstance>(Component);
    UMCPAnimationAppearanceTestNotify::CallCount = 0; UMCPAnimationTestNotify::CallCount = 0;
    auto Instance = MakeUnique<FAnimMontageInstance>(AnimInstance);
    AnimInstance->MontageInstances.Add(Instance.Get());
    Instance->Initialize(Montage); Instance->Play(); Instance->UpdateWeight(1.0f); Instance->Advance(0.04f, nullptr, false);
    AnimInstance->MontageInstances.Remove(Instance.Get());
    TestEqual(TEXT("Montage dispatches one appearance Notify"), UMCPAnimationAppearanceTestNotify::CallCount, 1); TestEqual(TEXT("Gameplay Fire still once"), UMCPAnimationTestNotify::CallCount, 1);
    TestEqual(TEXT("Notify sends actual Niagara component User Float"), UMCPAnimationAppearanceTestNotify::DeliveredSize, 0.1f);
    TestEqual(TEXT("Shared system default remains 1"), System->GetExposedParameters().GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Size"))), 1.0f);
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("NotifyPropertiesAcceptance") / FPackageName::GetShortName(Root) / TEXT("snapshot.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt), true); TestTrue(TEXT("Save cold oracle"), FFileHelper::SaveStringToFile(Json(ReadNotifyProperties(ReaderParams)), *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("Notify properties fixture: ") + Root + TEXT(" event_guid=") + Guid.ToString(EGuidFormats::DigitsWithHyphens)); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationNotifyPropertiesReloadTest, "UnrealMCP.Animation.NotifyPropertiesCold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPAnimationNotifyPropertiesReloadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPAnimation; FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPNotifyPropertiesReload="), Root)) { AddInfo(TEXT("Explicit fixture root required; no cold evidence")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/NotifyProperties_"))) return false;
    FString Expected; const FString File = FPaths::ProjectSavedDir() / TEXT("NotifyPropertiesAcceptance") / FPackageName::GetShortName(Root) / TEXT("snapshot.json");
    if (!TestTrue(TEXT("Cold oracle exists"), FFileHelper::LoadFileToString(Expected, *File))) return false;
    TSharedPtr<FJsonObject> Oracle; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Expected), Oracle)) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/AM_Test")); Params->SetStringField(TEXT("event_guid"), Oracle->GetStringField(TEXT("event_guid")));
    const auto Actual = ReadNotifyProperties(Params);
    TestEqual(TEXT("Cold full scope/config/GUID/Outer/witness identical"), Json(Actual), Expected);
    TestFalse(TEXT("Cold clean"), Actual->GetBoolField(TEXT("package_dirty"))); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationReloadTest, "UnrealMCP.Animation.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPAnimationReloadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPAnimation;
    FString Root, NotifyRoot; FParse::Value(FCommandLine::Get(), TEXT("MCPAnimationReload="), Root); FParse::Value(FCommandLine::Get(), TEXT("MCPAnimationNotifyReload="), NotifyRoot);
    if (Root.IsEmpty() && NotifyRoot.IsEmpty()) { AddInfo(TEXT("Explicit roots required; cold read not exercised in this batch")); return true; }
    auto Check = [&](const FString& Folder, const FString& AssetName, const FString& Oracle)
    {
        if (!Folder.StartsWith(TEXT("/Game/__Dev/"))) { AddError(TEXT("Reload restricted to __Dev fixtures")); return; }
        FString Expected; const FString File = FPaths::ProjectSavedDir() / TEXT("AnimationAcceptance") / FPackageName::GetShortName(Folder) / Oracle;
        if (!TestTrue(TEXT("Cold oracle exists"), FFileHelper::LoadFileToString(Expected, *File))) return;
        auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Folder + TEXT("/") + AssetName);
        const auto State = Read(Params); TestEqual(TEXT("Cold full scope/object/GUID snapshot identical"), Json(State), Expected);
        if (auto* Sequence = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(Folder + TEXT("/") + AssetName)))
        {
            TArray<UAnimSequence*> Targets{Sequence}; UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Targets);
            const double Value = SampleRoot(Sequence, Sequence->GetPlayLength());
            TestTrue(FString::Printf(TEXT("Cold compressed additive pose is delta8, actual=%f"), Value), FMath::IsNearlyEqual(Value, 8.0, 0.001));
        }
        TestFalse(TEXT("Cold read not dirty"), State->GetBoolField(TEXT("package_dirty")));
    };
    if (!Root.IsEmpty()) Check(Root, TEXT("A_Test"), TEXT("sequence.json"));
    if (!NotifyRoot.IsEmpty()) Check(NotifyRoot, TEXT("AM_Test"), TEXT("montage.json"));
    return !HasAnyErrors();
}
#endif