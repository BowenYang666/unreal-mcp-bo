#include "Commands/UnrealMCPComponentDefaults.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealMCPComponentDefaults
{
    namespace
    {
        FString Json(const TSharedPtr<FJsonObject>& Value)
        {
            FString Text; FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
        }

        TSharedPtr<FJsonObject> Failure(const FString& Error)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), false); Result->SetBoolField(TEXT("modified"), false);
            Result->SetBoolField(TEXT("compiled"), false); Result->SetBoolField(TEXT("saved"), false);
            Result->SetStringField(TEXT("stage"), TEXT("preflight")); Result->SetStringField(TEXT("error"), Error); return Result;
        }

        UBlueprint* Load(const TSharedPtr<FJsonObject>& Params, FString& Error)
        {
            FString Path;
            if (!Params || !Params->TryGetStringField(TEXT("blueprint_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
                || Path.Len() >= NAME_SIZE || Path.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Path))
            { Error = TEXT("Require an exact /Game Blueprint package path"); return nullptr; }
            auto* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
            if (!Blueprint || Blueprint->GetPackage()->GetName() != Path || !Blueprint->GeneratedClass
                || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()) || Blueprint->BlueprintType != BPTYPE_Normal)
            { Error = TEXT("Require an existing normal Actor Blueprint, not a redirector or unsupported asset"); return nullptr; }
            return Blueprint;
        }

        struct FComponent
        {
            FString Source;
            FString Name;
            UClass* Declaring = nullptr;
            USCS_Node* Node = nullptr;
            UActorComponent* Template = nullptr;
            bool OwnOverride = false;

            TSharedRef<FJsonObject> Selector() const
            {
                auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("source"), Source); Result->SetStringField(TEXT("name"), Name);
                Result->SetStringField(TEXT("declaring_class"), Declaring->GetPathName());
                Result->SetStringField(TEXT("scs_guid"), Node ? Node->VariableGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("")); return Result;
            }
        };

        TArray<FComponent> Resolve(UBlueprint* Blueprint, TArray<FString>& Errors)
        {
            TArray<FComponent> Result; auto* Generated = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
            if (!Generated) { Errors.Add(TEXT("Generated class unavailable")); return Result; }
            if (Generated->GetInheritableComponentHandler(false) != Blueprint->GetInheritableComponentHandler(false)) Errors.Add(TEXT("Source/generated inherited handler mismatch"));
            int32 Depth = 0;
            for (auto* Class = Generated; Class; Class = Cast<UBlueprintGeneratedClass>(Class->GetSuperClass()))
            {
                if (++Depth > 32) { Errors.Add(TEXT("Blueprint inheritance exceeds 32 levels")); break; }
                if (!Class->SimpleConstructionScript) continue;
                const auto& Nodes = Class->SimpleConstructionScript->GetAllNodes();
                if (Nodes.Num() > 512) { Errors.Add(TEXT("SCS node bound exceeded")); break; }
                for (auto* Node : Nodes)
                {
                    if (!Node || !Node->ComponentClass || !Node->ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass())) continue;
                    FComponent Item; Item.Source = Class == Generated ? TEXT("local_scs") : TEXT("inherited_scs");
                    Item.Name = Node->GetVariableName().ToString(); Item.Declaring = Class; Item.Node = Node;
                    Item.Template = Node->GetActualComponentTemplate(Generated);
                    if (auto* Handler = Generated->GetInheritableComponentHandler(false)) Item.OwnOverride = Handler->GetOverridenComponentTemplate(FComponentKey(Node)) != nullptr;
                    Result.Add(Item);
                    if (Result.Num() > 256) { Errors.Add(TEXT("Component bound exceeded")); return Result; }
                }
            }
            auto* Default = Cast<AActor>(Generated->GetDefaultObject(false));
            if (!Default) { Errors.Add(TEXT("Existing target CDO unavailable; reader never constructs or compiles it")); return Result; }
            TInlineComponentArray<UActorComponent*> Native; Default->GetComponents(Native);
            for (auto* Component : Native)
            {
                if (!Component || !Component->IsA<USkeletalMeshComponent>() || Component->CreationMethod != EComponentCreationMethod::Native) continue;
                FComponent Item; Item.Source = TEXT("cpp_inherited"); Item.Name = Component->GetName(); Item.Template = Component;
                for (UClass* Class = Generated->GetSuperClass(); Class && Class->IsChildOf(AActor::StaticClass()); Class = Class->GetSuperClass())
                {
                    auto* Ancestor = Class->GetDefaultObject(false);
                    if (Ancestor && Ancestor->GetDefaultSubobjectByName(Component->GetFName())) Item.Declaring = Class;
                }
                if (!Item.Declaring || Item.Declaring->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || Component->GetOuter() != Default || !Component->HasAnyFlags(RF_DefaultSubObject))
                { Errors.Add(TEXT("Cannot prove native declaring class/target CDO ownership: ") + Item.Name); continue; }
                Result.Add(Item);
                if (Result.Num() > 256) { Errors.Add(TEXT("Component bound exceeded")); return Result; }
            }
            Result.Sort([](const FComponent& Left, const FComponent& Right) { return Json(Left.Selector()) < Json(Right.Selector()); });
            TSet<FString> Seen;
            for (const auto& Item : Result)
            {
                if (Seen.Contains(Item.Name)) Errors.Add(TEXT("Ambiguous component name: ") + Item.Name);
                Seen.Add(Item.Name);
            }
            return Result;
        }

        TSharedRef<FJsonObject> Defaults(USkeletalMeshComponent* Component)
        {
            auto Result = MakeShared<FJsonObject>();
            if (const auto* Mesh = Component->GetSkeletalMeshAsset()) Result->SetStringField(TEXT("mesh"), Mesh->GetPathName()); else Result->SetField(TEXT("mesh"), MakeShared<FJsonValueNull>());
            if (const auto* Class = Component->GetAnimClass()) Result->SetStringField(TEXT("anim_class"), Class->GetPathName()); else Result->SetField(TEXT("anim_class"), MakeShared<FJsonValueNull>());
            Result->SetStringField(TEXT("animation_mode"), StaticEnum<EAnimationMode::Type>()->GetNameStringByValue(Component->GetAnimationMode())); return Result;
        }

        TSharedRef<FJsonObject> Witness(const UActorComponent* Component, TArray<FString>& Errors)
        {
            auto Result = MakeShared<FJsonObject>(); TArray<FProperty*> Properties;
            for (TFieldIterator<FProperty> Field(Component->GetClass()); Field; ++Field)
            {
                if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
                if (Field->GetFName() == TEXT("SkinnedAsset") || Field->GetFName() == TEXT("SkeletalMesh") || Field->GetFName() == TEXT("SkeletalMeshAsset")
                    || Field->GetFName() == TEXT("AnimClass") || Field->GetFName() == TEXT("AnimationMode")) continue;
                Properties.Add(*Field);
            }
            Properties.Sort([](const FProperty& Left, const FProperty& Right) { return Left.GetName() < Right.GetName(); });
            int32 Characters = 0;
            for (auto* Property : Properties)
            {
                FString Text; Property->ExportTextItem_Direct(Text, Property->ContainerPtrToValuePtr<void>(Component), nullptr, nullptr, PPF_None);
                Characters += Text.Len(); if (Text.Len() > 65536 || Characters > 524288) { Errors.Add(TEXT("Saved-field preservation witness exceeds bound")); break; }
                Result->SetStringField(Property->GetName(), Text);
            }
            return Result;
        }

        FString FileHash(const UObject* Object)
        {
            if (!Object || !Object->GetPackage()->GetName().StartsWith(TEXT("/Game/"))) return TEXT("");
            const FString File = FPackageName::LongPackageNameToFilename(Object->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
            return IFileManager::Get().FileExists(*File) ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("unsaved_package");
        }

        TSharedRef<FJsonObject> Snapshot(UBlueprint* Blueprint)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("component_defaults_contract"), 1);
            Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPackage()->GetName()); Result->SetStringField(TEXT("class_path"), Blueprint->GeneratedClass->GetPathName());
            TArray<FString> Errors; const auto Components = Resolve(Blueprint, Errors); TArray<TSharedPtr<FJsonValue>> Rows, Hierarchy;
            int32 Depth = 0;
            for (UClass* Class = Blueprint->GeneratedClass.Get(); Class && Class->IsChildOf(AActor::StaticClass()) && Depth++ < 64; Class = Class->GetSuperClass())
            {
                auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("class_path"), Class->GetPathName());
                if (const auto* Parent = Cast<UBlueprint>(Class->ClassGeneratedBy)) Entry->SetStringField(TEXT("blueprint_path"), Parent->GetPackage()->GetName());
                                if (const auto* Parent = Cast<UBlueprint>(Class->ClassGeneratedBy)) Entry->SetStringField(TEXT("saved_package_hash"), FileHash(Parent));
                Hierarchy.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Result->SetArrayField(TEXT("hierarchy"), Hierarchy);
            auto ActorFields = MakeShared<FJsonObject>(); int32 ActorCharacters = 0;
            if (const auto* Default = Blueprint->GeneratedClass->GetDefaultObject(false))
            {
                for (TFieldIterator<FProperty> Field(Default->GetClass()); Field; ++Field)
                {
                    if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
                    FString Text; Field->ExportTextItem_Direct(Text, Field->ContainerPtrToValuePtr<void>(Default), nullptr, nullptr, PPF_None);
                    ActorCharacters += Text.Len(); if (Text.Len() > 65536 || ActorCharacters > 524288) { Errors.Add(TEXT("Actor saved-field witness exceeds bound")); break; }
                    ActorFields->SetStringField(Field->GetName(), Text);
                }
            }
            Result->SetObjectField(TEXT("preserved_actor_fields"), ActorFields);
            TArray<TSharedPtr<FJsonValue>> Structure;
            if (Blueprint->SimpleConstructionScript) for (const auto* Node : Blueprint->SimpleConstructionScript->GetAllNodes()) if (Node)
            {
                auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Node->GetVariableName().ToString()); Entry->SetStringField(TEXT("guid"), Node->VariableGuid.ToString());
                Entry->SetStringField(TEXT("class"), GetPathNameSafe(Node->ComponentClass)); Entry->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
                Structure.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Result->SetArrayField(TEXT("local_scs_structure"), Structure);
            for (const auto& Item : Components)
            {
                auto Row = MakeShared<FJsonObject>(); Row->SetObjectField(TEXT("component"), Item.Selector());
                auto* Component = Cast<USkeletalMeshComponent>(Item.Template); const int32 Before = Errors.Num();
                if (!Component) { Errors.Add(TEXT("Missing effective template")); Row->SetBoolField(TEXT("complete"), false); Rows.Add(MakeShared<FJsonValueObject>(Row)); continue; }
                Row->SetStringField(TEXT("class_path"), Component->GetClass()->GetPathName()); Row->SetStringField(TEXT("template_path"), Component->GetPathName());
                Row->SetStringField(TEXT("outer_path"), Component->GetOuter()->GetPathName()); Row->SetStringField(TEXT("archetype_path"), Component->GetArchetype()->GetPathName());
                Row->SetBoolField(TEXT("target_owns_override"), Item.OwnOverride); Row->SetBoolField(TEXT("editable_when_inherited"), Component->bEditableWhenInherited);
                Row->SetObjectField(TEXT("defaults"), Defaults(Component)); Row->SetObjectField(TEXT("preserved_fields"), Witness(Component, Errors));
                auto Dependencies = MakeShared<FJsonObject>();
                if (const auto* Mesh = Component->GetSkeletalMeshAsset())
                {
                    Dependencies->SetStringField(TEXT("mesh_hash"), FileHash(Mesh));
                    if (const auto* Skeleton = Mesh->GetSkeleton()) { Dependencies->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName()); Dependencies->SetStringField(TEXT("skeleton_guid"), Skeleton->GetGuid().ToString()); Dependencies->SetStringField(TEXT("skeleton_hash"), FileHash(Skeleton)); }
                }
                if (const auto* Class = Component->GetAnimClass()) Dependencies->SetStringField(TEXT("anim_class_hash"), FileHash(Class));
                Row->SetObjectField(TEXT("dependencies"), Dependencies);
                if (Component->GetClass() != USkeletalMeshComponent::StaticClass()) Errors.Add(TEXT("Derived component native storage unsupported"));
                if (Item.Node && !Item.Node->VariableGuid.IsValid()) Errors.Add(TEXT("Missing SCS identity GUID"));
                Row->SetBoolField(TEXT("complete"), Before == Errors.Num()); Rows.Add(MakeShared<FJsonValueObject>(Row));
            }
            Result->SetArrayField(TEXT("components"), Rows); Result->SetBoolField(TEXT("complete"), Errors.IsEmpty());
            TArray<TSharedPtr<FJsonValue>> Unsupported; for (const auto& Error : Errors) Unsupported.Add(MakeShared<FJsonValueString>(Error)); Result->SetArrayField(TEXT("unsupported"), Unsupported);
            const auto Text = Json(Result); const FTCHARToUTF8 Bytes(*Text); FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
            if (Errors.IsEmpty()) Result->SetStringField(TEXT("baseline"), Hash.ToString()); else Result->SetField(TEXT("baseline"), MakeShared<FJsonValueNull>());
            Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetPackage()->IsDirty());
            Result->SetStringField(TEXT("semantics"), TEXT("SkeletalMeshComponent default authoring scope. Typed three-field values plus read-only saved reflected preservation witness; not arbitrary component serialization. No override creation, compile, spawn or save on read.")); return Result;
        }

        FComponent* Find(TArray<FComponent>& Components, const TSharedPtr<FJsonObject>& Selector)
        {
            if (!Selector || Selector->Values.Num() != 4) return nullptr;
            for (const auto* Key : {TEXT("source"), TEXT("name"), TEXT("declaring_class"), TEXT("scs_guid")})
                if (!Selector->HasTypedField<EJson::String>(Key)) return nullptr;
            return Components.FindByPredicate([&Selector](const FComponent& Item)
            {
                const auto Key = Item.Selector();
                for (const auto& Field : Key->Values) if (Field.Value->AsString() != Selector->GetStringField(Field.Key)) return false;
                return true;
            });
        }

        const TSharedPtr<FJsonObject> RowFor(const TSharedPtr<FJsonObject>& State, const FComponent& Component)
        {
            const auto Key = Component.Selector();
            for (const auto& Value : State->GetArrayField(TEXT("components")))
            {
                const auto Row = Value->AsObject(); const auto Candidate = Row->GetObjectField(TEXT("component")); bool Same = true;
                for (const auto& Field : Key->Values) Same &= Candidate->GetStringField(Field.Key) == Field.Value->AsString();
                if (Same) return Row;
            }
            return nullptr;
        }

#if WITH_DEV_AUTOMATION_TESTS
        bool FailSaveForTest = false;
#endif
    }

    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; auto* Blueprint = Load(Params, Error); if (!Blueprint) return Failure(Error);
        auto Result = Snapshot(Blueprint); Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("compiled"), false); Result->SetBoolField(TEXT("saved"), false); return Result;
    }

    TSharedPtr<FJsonObject> Write(const TSharedPtr<FJsonObject>& Params)
    {
        if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask || FAssetCompilingManager::Get().GetNumRemainingAssets()
            || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid())) return Failure(TEXT("Require an idle Editor outside PIE/save/GC/modal/asset compilation"));
        if (!Params) return Failure(TEXT("Missing arguments"));
        const TSet<FString> Allowed{TEXT("blueprint_path"), TEXT("component"), TEXT("expected_baseline"), TEXT("settings"), TEXT("dry_run"), TEXT("save")};
        for (const auto& Pair : Params->Values) if (!Allowed.Contains(Pair.Key)) return Failure(TEXT("Unknown argument: ") + Pair.Key);
        FString Error; auto* Blueprint = Load(Params, Error); if (!Blueprint) return Failure(Error);
        TArray<UBlueprint*> Ancestors;
        for (UClass* Class = Blueprint->GeneratedClass.Get(); Class; Class = Class->GetSuperClass())
        {
            if (auto* Owner = Cast<UBlueprint>(Class->ClassGeneratedBy))
            {
                if (Owner->GetPackage()->IsDirty() || Owner->bBeingCompiled || (Owner->Status != BS_UpToDate && Owner->Status != BS_UpToDateWithWarnings))
                    return Failure(TEXT("Target/ancestor Blueprint is dirty, uncompiled or compiling; no implicit save or parent compile"));
                if (Owner != Blueprint) Ancestors.Add(Owner);
            }
        }
        const auto Before = Snapshot(Blueprint); FString Expected;
        if (!Before->GetBoolField(TEXT("complete"))) return Failure(TEXT("Incomplete/ambiguous component scope; review unsupported before authoring"));
        if (!Params->TryGetStringField(TEXT("expected_baseline"), Expected) || Expected != Before->GetStringField(TEXT("baseline"))) return Failure(TEXT("Missing/stale expected_baseline"));
        const TSharedPtr<FJsonObject>* Selector = nullptr; const TSharedPtr<FJsonObject>* Settings = nullptr;
        if (!Params->TryGetObjectField(TEXT("component"), Selector) || !Params->TryGetObjectField(TEXT("settings"), Settings) || (*Settings)->Values.IsEmpty()) return Failure(TEXT("Require exact component selector and nonempty settings"));
        TArray<FString> Errors; auto Components = Resolve(Blueprint, Errors); auto* Found = Find(Components, *Selector);
        if (!Found || !Errors.IsEmpty()) return Failure(TEXT("Component identity is missing, ambiguous or stale"));
        const FComponent Identity = *Found; auto* Component = Cast<USkeletalMeshComponent>(Identity.Template);
        const FName OriginalObjectName = Component ? Component->GetFName() : NAME_None;
        if (!Component || Component->GetClass() != USkeletalMeshComponent::StaticClass() || !Component->IsTemplate() || !Component->bEditableWhenInherited)
            return Failure(TEXT("Require an editable exact SkeletalMeshComponent template"));
        bool DryRun = true, SaveAsset = false;
        if ((Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) || (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), SaveAsset))) return Failure(TEXT("dry_run/save must be bool"));
        const TSet<FString> Fields{TEXT("mesh"), TEXT("animation_mode"), TEXT("anim_class")};
        auto Proposed = MakeShared<FJsonObject>(*Defaults(Component));
        for (const auto& Pair : (*Settings)->Values) { if (!Fields.Contains(Pair.Key)) return Failure(TEXT("Unsupported component field: ") + Pair.Key); Proposed->SetField(Pair.Key, Pair.Value); }
        FString Mode; if (!Proposed->TryGetStringField(TEXT("animation_mode"), Mode) || Mode != TEXT("AnimationBlueprint") || Component->GetAnimationMode() != EAnimationMode::AnimationBlueprint)
            return Failure(TEXT("First contract supports only AnimationBlueprint mode; no SingleNode/custom animation-data transitions"));
        USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset(); UClass* AnimClass = Component->GetAnimClass(); UAnimBlueprint* AnimBlueprint = nullptr;
        if (const auto* Value = (*Settings)->Values.Find(TEXT("mesh")))
        {
            if ((*Value)->IsNull()) Mesh = nullptr;
            else
            {
                FString Path; if (!(*Value)->TryGetString(Path) || !Path.StartsWith(TEXT("/Game/")) || Path.Len() >= NAME_SIZE) return Failure(TEXT("mesh requires null or exact /Game SkeletalMesh package/object path"));
                Mesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(Path));
                if (!Mesh || (Mesh->GetPathName() != Path && Mesh->GetPackage()->GetName() != Path)) return Failure(TEXT("Wrong/noncanonical/redirected SkeletalMesh reference"));
            }
        }
        if (const auto* Value = (*Settings)->Values.Find(TEXT("anim_class")))
        {
            if ((*Value)->IsNull()) AnimClass = nullptr;
            else
            {
                FString Path; if (!(*Value)->TryGetString(Path) || !Path.StartsWith(TEXT("/Game/")) || Path.Len() >= NAME_SIZE || !Path.EndsWith(TEXT("_C"))) return Failure(TEXT("anim_class requires a full /Game AnimBlueprint generated-class object path or null"));
                const FString Package = FPackageName::ObjectPathToPackageName(Path);
                if (!FPackageName::IsValidLongPackageName(Package)) return Failure(TEXT("Invalid AnimBlueprint package"));
                AnimBlueprint = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(Package)); AnimClass = AnimBlueprint ? AnimBlueprint->GeneratedClass.Get() : nullptr;
                if (!AnimClass || AnimClass->GetPathName() != Path) return Failure(TEXT("Not a matching AnimBlueprint generated class; do not infer _C from arbitrary assets"));
            }
        }
        if (!AnimBlueprint && AnimClass) AnimBlueprint = Cast<UAnimBlueprint>(AnimClass->ClassGeneratedBy);
        if ((Mesh == nullptr) != (AnimClass == nullptr)) return Failure(TEXT("Mesh and Anim Class must both be valid or explicitly cleared together"));
        auto* AnimGenerated = Cast<UAnimBlueprintGeneratedClass>(AnimClass);
        if (Mesh && (!Mesh->GetSkeleton() || Mesh->GetPackage()->IsDirty() || Mesh->GetSkeleton()->GetPackage()->IsDirty() || !AnimGenerated || !AnimClass->IsChildOf(UAnimInstance::StaticClass())
            || AnimClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || AnimGenerated->GetTargetSkeleton() != Mesh->GetSkeleton()
            || !AnimBlueprint || AnimBlueprint->GetPackage()->IsDirty() || AnimBlueprint->bBeingCompiled || (AnimBlueprint->Status != BS_UpToDate && AnimBlueprint->Status != BS_UpToDateWithWarnings)))
            return Failure(TEXT("Require clean Mesh/Skeleton/compiled AnimBlueprint with exact same Skeleton and concrete UAnimInstance class"));
        if (FAssetCompilingManager::Get().GetNumRemainingAssets()) return Failure(TEXT("References are loading/compiling; review after ready, no global compilation finish"));
        if (Mesh) Proposed->SetStringField(TEXT("mesh"), Mesh->GetPathName()); else Proposed->SetField(TEXT("mesh"), MakeShared<FJsonValueNull>());
        if (AnimClass) Proposed->SetStringField(TEXT("anim_class"), AnimClass->GetPathName()); else Proposed->SetField(TEXT("anim_class"), MakeShared<FJsonValueNull>());
        const bool Changed = Json(Proposed) != Json(Defaults(Component));
        auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("component_defaults_contract"), 1); Result->SetBoolField(TEXT("success"), true);
        Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetBoolField(TEXT("would_modify"), Changed); Result->SetBoolField(TEXT("modified"), false);
        Result->SetBoolField(TEXT("compiled"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("package_dirty"), false);
        Result->SetObjectField(TEXT("component"), *Selector); Result->SetObjectField(TEXT("before"), Before); Result->SetObjectField(TEXT("proposed_defaults"), Proposed); Result->SetObjectField(TEXT("after"), Before);
        if (DryRun || !Changed) return Result;
        TArray<FString> ParentStates; for (auto* Ancestor : Ancestors) ParentStates.Add(Json(Snapshot(Ancestor)));
        const auto BeforeRow = RowFor(Before, Identity);
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "ComponentDefaults", "Set Blueprint skeletal component defaults"));
            Blueprint->Modify(); auto* Generated = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
            if (Identity.Source == TEXT("inherited_scs"))
            {
                auto* Handler = Blueprint->GetInheritableComponentHandler(true); Handler->SetFlags(RF_Transactional); Handler->Modify();
                Component = Cast<USkeletalMeshComponent>(Handler->CreateOverridenComponentTemplate(FComponentKey(Identity.Node)));
            }
            if (!Component || (Identity.Source != TEXT("cpp_inherited") && !Component->IsIn(Generated)) || (Identity.Source == TEXT("cpp_inherited") && Component->GetOuter() != Generated->GetDefaultObject(false)))
            {
                Result->SetBoolField(TEXT("success"), false); Result->SetBoolField(TEXT("modified"), true); Result->SetStringField(TEXT("stage"), TEXT("resolve_override"));
                Result->SetStringField(TEXT("error"), TEXT("Target-owned template could not be established; transaction may contain changes, not saved"));
                Result->SetObjectField(TEXT("after"), Snapshot(Blueprint)); Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetPackage()->IsDirty()); return Result;
            }
            if (auto* Owner = Component->GetOuter(); Owner && !Owner->IsA<UClass>()) { Owner->SetFlags(RF_Transactional); Owner->Modify(); }
            Component->SetFlags(RF_Transactional); Component->Modify(); Component->PreEditChange(nullptr);
            Component->SetSkeletalMesh(Mesh, false); Component->SetAnimInstanceClass(AnimClass); Component->SetAnimationMode(EAnimationMode::AnimationBlueprint, false);
            Component->PostEditChange(); FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint); Blueprint->MarkPackageDirty();
        Result->SetBoolField(TEXT("modified"), true); const bool Compiled = Blueprint->Status != BS_Error && Blueprint->GeneratedClass;
        Result->SetBoolField(TEXT("compiled"), Compiled); auto After = Snapshot(Blueprint);
        Errors.Empty(); auto Resolved = Resolve(Blueprint, Errors); auto* Actual = Find(Resolved, *Selector); auto* ActualComponent = Actual ? Cast<USkeletalMeshComponent>(Actual->Template) : nullptr;
        bool Preserved = Compiled && Errors.IsEmpty() && After->GetBoolField(TEXT("complete")) && ActualComponent && Json(Defaults(ActualComponent)) == Json(Proposed);
        Preserved &= Json(Before->GetObjectField(TEXT("preserved_actor_fields"))) == Json(After->GetObjectField(TEXT("preserved_actor_fields")));
        auto BeforeStructure = MakeShared<FJsonObject>(); BeforeStructure->SetArrayField(TEXT("nodes"), Before->GetArrayField(TEXT("local_scs_structure")));
        auto AfterStructure = MakeShared<FJsonObject>(); AfterStructure->SetArrayField(TEXT("nodes"), After->GetArrayField(TEXT("local_scs_structure"))); Preserved &= Json(BeforeStructure) == Json(AfterStructure);
        for (const auto& OldValue : Before->GetArrayField(TEXT("components")))
        {
            const auto Old = OldValue->AsObject(); if (Old->GetObjectField(TEXT("component"))->GetStringField(TEXT("name")) == Identity.Name) continue;
            const auto* Current = After->GetArrayField(TEXT("components")).FindByPredicate([&Old](const TSharedPtr<FJsonValue>& Item) { return Item->AsObject()->GetObjectField(TEXT("component"))->GetStringField(TEXT("name")) == Old->GetObjectField(TEXT("component"))->GetStringField(TEXT("name")); });
            Preserved &= Current && Json(Old) == Json((*Current)->AsObject());
        }
        if (Actual)
        {
            const auto AfterRow = RowFor(After, *Actual);
            Preserved &= BeforeRow && AfterRow && Json(BeforeRow->GetObjectField(TEXT("preserved_fields"))) == Json(AfterRow->GetObjectField(TEXT("preserved_fields")));
            if (Identity.Source == TEXT("inherited_scs")) Preserved &= Actual->OwnOverride && ActualComponent->IsIn(Blueprint->GeneratedClass.Get());
            if (Identity.Source == TEXT("cpp_inherited")) Preserved &= ActualComponent->GetOuter() == Blueprint->GeneratedClass->GetDefaultObject(false) && ActualComponent->GetFName() == OriginalObjectName;
        }
        for (int32 Index = 0; Index < Ancestors.Num(); ++Index) Preserved &= Json(Snapshot(Ancestors[Index])) == ParentStates[Index] && !Ancestors[Index]->GetPackage()->IsDirty();
        Result->SetBoolField(TEXT("existing_data_preserved"), Preserved);
        if (!Preserved)
        {
            Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), Compiled ? TEXT("readback") : TEXT("compile"));
            Result->SetStringField(TEXT("error"), TEXT("Compilation/effective values/preserved author data mismatch; actual changes remain unsaved, no rollback or retry"));
        }
        else if (SaveAsset)
        {
            bool Saved = false;
#if WITH_DEV_AUTOMATION_TESTS
            if (!FailSaveForTest)
#endif
                Saved = UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false) && !Blueprint->GetPackage()->IsDirty();
            Result->SetBoolField(TEXT("saved"), Saved);
            if (!Saved) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save")); Result->SetStringField(TEXT("error"), TEXT("Applied/compiled but save failed; inspect after and dirty state, no retry")); }
        }
        Result->SetObjectField(TEXT("after"), Snapshot(Blueprint)); Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetPackage()->IsDirty()); return Result;
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameFramework/Character.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace UnrealMCPComponentDefaults
{
    namespace
    {
        UObject* Fixture(UClass* Class, const FString& Root, const FString& Name)
        {
            auto* Object = NewObject<UObject>(CreatePackage(*(Root + TEXT("/") + Name)), Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Object); Object->MarkPackageDirty(); return Object;
        }

        UBlueprint* BlueprintFixture(UClass* Parent, const FString& Root, const FString& Name)
        {
            auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*(Root + TEXT("/") + Name)), FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
            FAssetRegistryModule::AssetCreated(Blueprint); return Blueprint;
        }

        USkeletalMeshComponent* ResolveFor(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Selector)
        {
            TArray<FString> Errors; auto All = Resolve(Blueprint, Errors); const auto* Found = Find(All, Selector); return Found ? Cast<USkeletalMeshComponent>(Found->Template) : nullptr;
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPComponentDefaultsReadTest, "UnrealMCP.ComponentDefaults.Read", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPComponentDefaultsReadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPComponentDefaults;
    const FString Root = TEXT("/Game/__Dev/ComponentDefaults_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Parent = FKismetEditorUtilities::CreateBlueprint(ACharacter::StaticClass(), CreatePackage(*(Root + TEXT("/BP_Parent"))), FName(TEXT("BP_Parent")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    auto* Node = Parent->SimpleConstructionScript->CreateNode(USkeletalMeshComponent::StaticClass(), FName(TEXT("AddedMesh"))); Parent->SimpleConstructionScript->AddNode(Node);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Parent); FKismetEditorUtilities::CompileBlueprint(Parent);
    auto* Child = FKismetEditorUtilities::CreateBlueprint(Parent->GeneratedClass, CreatePackage(*(Root + TEXT("/BP_Child"))), FName(TEXT("BP_Child")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    const bool Dirty = Child->GetPackage()->IsDirty(); const auto Before = Snapshot(Child);
    if (!TestTrue(TEXT("Native and inherited SCS read complete"), Before->GetBoolField(TEXT("complete")))) { AddError(Json(Before)); return false; }
    TestEqual(TEXT("Two skeletal sources, not duplicated native mesh"), Before->GetArrayField(TEXT("components")).Num(), 2);
    TSet<FString> Origins; for (const auto& Row : Before->GetArrayField(TEXT("components"))) Origins.Add(Row->AsObject()->GetObjectField(TEXT("component"))->GetStringField(TEXT("source")));
    TestTrue(TEXT("Native origin proven"), Origins.Contains(TEXT("cpp_inherited"))); TestTrue(TEXT("Parent SCS origin proven"), Origins.Contains(TEXT("inherited_scs")));
    TestTrue(TEXT("Reader creates no target handler"), !Cast<UBlueprintGeneratedClass>(Child->GeneratedClass)->GetInheritableComponentHandler(false));
    TestEqual(TEXT("Reader leaves dirty state"), Child->GetPackage()->IsDirty(), Dirty);
    TestEqual(TEXT("Reader baseline deterministic"), Snapshot(Child)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPComponentDefaultsWriteTest, "UnrealMCP.ComponentDefaults.Write", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPComponentDefaultsWriteTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPComponentDefaults;
    const FString Root = TEXT("/Game/__Dev/ComponentDefaultsWrite_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Skeleton = Cast<USkeleton>(Fixture(USkeleton::StaticClass(), Root, TEXT("SKEL_Test")));
    auto* Mesh = Cast<USkeletalMesh>(Fixture(USkeletalMesh::StaticClass(), Root, TEXT("SKM_Test")));
    {
        FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), nullptr); Modifier.Add(FMeshBoneInfo(FName(TEXT("root")), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    Skeleton->MergeAllBonesToBoneTree(Mesh); Mesh->SetSkeleton(Skeleton);
    auto* Alternate = Cast<USkeletalMesh>(Fixture(USkeletalMesh::StaticClass(), Root, TEXT("SKM_Alternate")));
    {
        FReferenceSkeletonModifier Modifier(Alternate->GetRefSkeleton(), nullptr); Modifier.Add(FMeshBoneInfo(FName(TEXT("root")), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    Alternate->SetSkeleton(Skeleton);
    auto* Factory = NewObject<UAnimBlueprintFactory>(); Factory->TargetSkeleton = Skeleton; Factory->ParentClass = UAnimInstance::StaticClass();
    auto* Anim = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), CreatePackage(*(Root + TEXT("/ABP_Test"))), FName(TEXT("ABP_Test")), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!TestNotNull(TEXT("Animation Blueprint fixture"), Anim)) return false;
    FKismetEditorUtilities::CompileBlueprint(Anim); FAssetRegistryModule::AssetCreated(Anim);
    TArray<UObject*> Ready{Mesh, Alternate, Anim}; FAssetCompilingManager::Get().FinishCompilationForObjects(Ready);
    for (auto* Asset : TArray<UObject*>{Skeleton, Mesh, Alternate, Anim}) if (!TestTrue(TEXT("Reference fixture saved"), UEditorAssetLibrary::SaveLoadedAsset(Asset, false))) return false;
    auto* Parent = BlueprintFixture(ACharacter::StaticClass(), Root, TEXT("BP_Parent"));
    auto* Node = Parent->SimpleConstructionScript->CreateNode(USkeletalMeshComponent::StaticClass(), FName(TEXT("AddedMesh"))); Parent->SimpleConstructionScript->AddNode(Node);
    auto* Template = Cast<USkeletalMeshComponent>(Node->ComponentTemplate); Template->SetSkeletalMesh(Mesh, false); Template->SetAnimInstanceClass(Anim->GeneratedClass);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Parent); FKismetEditorUtilities::CompileBlueprint(Parent);
    if (!TestTrue(TEXT("Parent fixture saved"), UEditorAssetLibrary::SaveLoadedAsset(Parent, false))) return false;
    auto* Child = BlueprintFixture(Parent->GeneratedClass, Root, TEXT("BP_Child"));
    auto* Grandchild = BlueprintFixture(Child->GeneratedClass, Root, TEXT("BP_Grandchild"));
    auto* Local = Grandchild->SimpleConstructionScript->CreateNode(USkeletalMeshComponent::StaticClass(), FName(TEXT("LocalMesh"))); Grandchild->SimpleConstructionScript->AddNode(Local);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Grandchild); FKismetEditorUtilities::CompileBlueprint(Grandchild);
    auto* Sibling = BlueprintFixture(Child->GeneratedClass, Root, TEXT("BP_Sibling"));
    for (auto* Asset : {Child, Grandchild, Sibling}) if (!TestTrue(TEXT("Blueprint fixture saved"), UEditorAssetLibrary::SaveLoadedAsset(Asset, false))) return false;
    const auto ParentBefore = Json(Snapshot(Parent)); const auto ChildBefore = Json(Snapshot(Child)); const auto SiblingBefore = Json(Snapshot(Sibling));
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("blueprint_path"), Grandchild->GetPackage()->GetName());
    auto Refresh = [&]() { const auto State = Snapshot(Grandchild); Params->SetStringField(TEXT("expected_baseline"), State->GetStringField(TEXT("baseline"))); return State; };
    const auto Before = Refresh(); if (!TestTrue(TEXT("All three source kinds readable"), Before->GetBoolField(TEXT("complete")))) { AddError(Json(Before)); return false; }
    auto SelectorFor = [&Before](const FString& Source)
    {
        for (const auto& Row : Before->GetArrayField(TEXT("components"))) if (Row->AsObject()->GetObjectField(TEXT("component"))->GetStringField(TEXT("source")) == Source) return Row->AsObject()->GetObjectField(TEXT("component"));
        return TSharedPtr<FJsonObject>();
    };
    auto Native = SelectorFor(TEXT("cpp_inherited")); auto Inherited = SelectorFor(TEXT("inherited_scs")); auto LocalKey = SelectorFor(TEXT("local_scs"));
    if (!TestTrue(TEXT("Three precise selectors"), Native.IsValid() && Inherited.IsValid() && LocalKey.IsValid())) return false;
    auto Settings = MakeShared<FJsonObject>(); Settings->SetStringField(TEXT("mesh"), Alternate->GetPathName()); Settings->SetStringField(TEXT("anim_class"), Anim->GeneratedClass->GetPathName()); Settings->SetStringField(TEXT("animation_mode"), TEXT("AnimationBlueprint"));
    Params->SetObjectField(TEXT("settings"), Settings); Params->SetObjectField(TEXT("component"), Native);
    const auto DiskBefore = FileHash(Grandchild); const auto Preview = Write(Params);
    if (!TestTrue(TEXT("Native preview valid"), Preview->GetBoolField(TEXT("success")))) { AddError(Json(Preview)); return false; }
    TestFalse(TEXT("Preview unchanged"), Preview->GetBoolField(TEXT("modified"))); TestEqual(TEXT("Preview file unchanged"), FileHash(Grandchild), DiskBefore);
    Params->SetBoolField(TEXT("dry_run"), false); const auto Draft = Write(Params);
    if (!TestTrue(TEXT("Native default applied"), Draft->GetBoolField(TEXT("success")))) { AddError(Json(Draft)); return false; }
    TestTrue(TEXT("Draft compiled/dirty, not saved"), Draft->GetBoolField(TEXT("compiled")) && Draft->GetBoolField(TEXT("package_dirty")) && !Draft->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Re-resolved native mesh"), ResolveFor(Grandchild, Native)->GetSkeletalMeshAsset(), Alternate);
    TestEqual(TEXT("Draft disk unchanged"), FileHash(Grandchild), DiskBefore);
    TestFalse(TEXT("Dirty target rejected"), Write(Params)->GetBoolField(TEXT("success")));
    GEditor->UndoTransaction(); FKismetEditorUtilities::CompileBlueprint(Grandchild);
    auto* Undone = ResolveFor(Grandchild, Native);
    if (!TestNotNull(TEXT("Native identity resolves after Undo/recompile"), Undone)) { AddError(Json(Snapshot(Grandchild))); return false; }
    TestNull(TEXT("Native Undo restores null mesh"), Undone->GetSkeletalMeshAsset());
    GEditor->RedoTransaction(); FKismetEditorUtilities::CompileBlueprint(Grandchild);
    TestEqual(TEXT("Native Redo mesh"), ResolveFor(Grandchild, Native)->GetSkeletalMeshAsset(), Alternate);
    GEditor->UndoTransaction(); FKismetEditorUtilities::CompileBlueprint(Grandchild); Grandchild->GetPackage()->SetDirtyFlag(false); Refresh(); Params->SetBoolField(TEXT("save"), true);
    FailSaveForTest = true; const auto Failed = Write(Params); FailSaveForTest = false;
    TestTrue(TEXT("Save failure retains modified/compiled"), !Failed->GetBoolField(TEXT("success")) && Failed->GetBoolField(TEXT("modified")) && Failed->GetBoolField(TEXT("compiled")) && !Failed->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Save failure stage"), Failed->GetStringField(TEXT("stage")), FString(TEXT("save"))); TestEqual(TEXT("Save failure disk unchanged"), FileHash(Grandchild), DiskBefore);
    GEditor->UndoTransaction(); FKismetEditorUtilities::CompileBlueprint(Grandchild); Grandchild->GetPackage()->SetDirtyFlag(false); Refresh();
    const auto SavedNative = Write(Params); if (!TestTrue(TEXT("Native target-only saved"), SavedNative->GetBoolField(TEXT("success")) && SavedNative->GetBoolField(TEXT("saved")))) { AddError(Json(SavedNative)); return false; }
    Refresh(); const auto Repeat = Write(Params); TestTrue(TEXT("Same native values no-op"), Repeat->GetBoolField(TEXT("success")) && !Repeat->GetBoolField(TEXT("modified")) && !Repeat->GetBoolField(TEXT("compiled")) && !Repeat->GetBoolField(TEXT("saved")));
    Params->SetObjectField(TEXT("component"), Inherited); Refresh();
    Params->SetBoolField(TEXT("dry_run"), true);
    const auto InheritedPreview = Write(Params); TestFalse(TEXT("Inherited preview no override creation"), RowFor(Snapshot(Grandchild), FComponent{TEXT("inherited_scs"), Inherited->GetStringField(TEXT("name")), Node->GetSCS()->GetOwnerClass(), Node, Node->ComponentTemplate, false})->GetBoolField(TEXT("target_owns_override")));
    Params->SetBoolField(TEXT("dry_run"), false);
    const auto SavedInherited = Write(Params); if (!TestTrue(TEXT("Target child inherited SCS override saved"), SavedInherited->GetBoolField(TEXT("success")) && SavedInherited->GetBoolField(TEXT("saved")))) { AddError(Json(SavedInherited)); return false; }
    GEditor->UndoTransaction(); FKismetEditorUtilities::CompileBlueprint(Grandchild);
    auto* InheritedUndone = ResolveFor(Grandchild, Inherited);
    if (!TestNotNull(TEXT("Inherited identity resolves after Undo/recompile"), InheritedUndone)) { AddError(Json(Snapshot(Grandchild))); return false; }
    TestEqual(TEXT("Inherited Undo effective parent value"), InheritedUndone->GetSkeletalMeshAsset(), Mesh);
    GEditor->RedoTransaction(); FKismetEditorUtilities::CompileBlueprint(Grandchild);
    TestEqual(TEXT("Inherited Redo target value"), ResolveFor(Grandchild, Inherited)->GetSkeletalMeshAsset(), Alternate);
    if (!TestTrue(TEXT("Explicit fixture recovery save"), UEditorAssetLibrary::SaveLoadedAsset(Grandchild, false))) return false;
    auto* Handler = Cast<UBlueprintGeneratedClass>(Grandchild->GeneratedClass)->GetInheritableComponentHandler(false);
    TestTrue(TEXT("Override belongs to exact child generated class"), Handler && Handler->GetOverridenComponentTemplate(FComponentKey(Node)) && Handler->GetOverridenComponentTemplate(FComponentKey(Node))->GetOuter() == Grandchild->GeneratedClass.Get());
    Params->SetObjectField(TEXT("component"), LocalKey); Refresh(); const auto SavedLocal = Write(Params);
    if (!TestTrue(TEXT("Local SCS also saved"), SavedLocal->GetBoolField(TEXT("success")) && SavedLocal->GetBoolField(TEXT("saved")))) { AddError(Json(SavedLocal)); return false; }
    Refresh(); Params->SetStringField(TEXT("expected_baseline"), TEXT("stale")); const auto Stale = Write(Params); TestFalse(TEXT("Stale refused before mutation"), Stale->GetBoolField(TEXT("success"))); TestFalse(TEXT("Stale no mutation"), Stale->GetBoolField(TEXT("modified")));
    Refresh(); Settings->SetStringField(TEXT("mesh"), Anim->GetPathName()); TestFalse(TEXT("Wrong Mesh type refused"), Write(Params)->GetBoolField(TEXT("success"))); Settings->SetStringField(TEXT("mesh"), Alternate->GetPathName());
    Settings->SetStringField(TEXT("anim_class"), Mesh->GetPathName()); TestFalse(TEXT("Wrong Anim Class refused"), Write(Params)->GetBoolField(TEXT("success"))); Settings->SetStringField(TEXT("anim_class"), Anim->GeneratedClass->GetPathName());
    Settings->SetStringField(TEXT("animation_mode"), TEXT("AnimationSingleNode")); TestFalse(TEXT("Unsupported mode transition refused"), Write(Params)->GetBoolField(TEXT("success"))); Settings->SetStringField(TEXT("animation_mode"), TEXT("AnimationBlueprint"));
    Settings->SetField(TEXT("mesh"), MakeShared<FJsonValueNull>()); TestFalse(TEXT("Partial clearing inconsistent combination refused"), Write(Params)->GetBoolField(TEXT("success"))); Settings->SetStringField(TEXT("mesh"), Alternate->GetPathName());
    auto WrongKey = MakeShared<FJsonObject>(*LocalKey); WrongKey->SetStringField(TEXT("name"), TEXT("NoSuchComponent")); Params->SetObjectField(TEXT("component"), WrongKey); TestFalse(TEXT("Incorrect component identity refused"), Write(Params)->GetBoolField(TEXT("success"))); Params->SetObjectField(TEXT("component"), LocalKey);
    auto* OtherSkeleton = Cast<USkeleton>(Fixture(USkeleton::StaticClass(), Root, TEXT("SKEL_Other"))); UEditorAssetLibrary::SaveLoadedAsset(OtherSkeleton, false);
    auto* WrongMesh = Cast<USkeletalMesh>(Fixture(USkeletalMesh::StaticClass(), Root, TEXT("SKM_Wrong")));
    {
        FReferenceSkeletonModifier Modifier(WrongMesh->GetRefSkeleton(), nullptr); Modifier.Add(FMeshBoneInfo(FName(TEXT("root")), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    OtherSkeleton->MergeAllBonesToBoneTree(WrongMesh); UEditorAssetLibrary::SaveLoadedAsset(OtherSkeleton, false);
    WrongMesh->SetSkeleton(OtherSkeleton); TArray<UObject*> WrongReady{WrongMesh}; FAssetCompilingManager::Get().FinishCompilationForObjects(WrongReady); UEditorAssetLibrary::SaveLoadedAsset(WrongMesh, false);
    Settings->SetStringField(TEXT("mesh"), WrongMesh->GetPathName()); TestFalse(TEXT("Different Skeleton refused"), Write(Params)->GetBoolField(TEXT("success"))); Settings->SetStringField(TEXT("mesh"), Alternate->GetPathName());
    TestEqual(TEXT("Parent unchanged"), Json(Snapshot(Parent)), ParentBefore); TestEqual(TEXT("Intermediate parent unchanged"), Json(Snapshot(Child)), ChildBefore); TestEqual(TEXT("Sibling unchanged"), Json(Snapshot(Sibling)), SiblingBefore);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Actor = World->SpawnActor<AActor>(Grandchild->GeneratedClass);
    if (TestNotNull(TEXT("Actual spawn"), Actor))
    {
        TInlineComponentArray<USkeletalMeshComponent*> Spawned; Actor->GetComponents(Spawned); TestEqual(TEXT("No duplicate components created"), Spawned.Num(), 3);
        for (auto* Component : Spawned) { TestEqual(TEXT("Spawned effective Mesh"), Component->GetSkeletalMeshAsset(), Alternate); TestEqual(TEXT("Spawned effective Anim Class"), Component->GetAnimClass(), Anim->GeneratedClass.Get()); }
        Actor->Destroy();
    }
    World->DestroyWorld(false);
    const FString Oracle = FPaths::ProjectSavedDir() / TEXT("ComponentDefaultsAcceptance") / FPackageName::GetShortName(Root) / TEXT("snapshot.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Oracle), true); TestTrue(TEXT("Cold oracle saved"), FFileHelper::SaveStringToFile(Json(Snapshot(Grandchild)), *Oracle, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(TEXT("Component defaults fixture: ") + Root); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPComponentDefaultsReloadTest, "UnrealMCP.ComponentDefaults.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPComponentDefaultsReloadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPComponentDefaults;
    FString Root; if (!FParse::Value(FCommandLine::Get(), TEXT("MCPComponentDefaultsReload="), Root)) { AddInfo(TEXT("Explicit reload root required; cold check skipped")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/")) || !FPackageName::IsValidLongPackageName(Root)) { AddError(TEXT("Reload restricted to __Dev fixtures")); return false; }
    FString Expected; const FString Oracle = FPaths::ProjectSavedDir() / TEXT("ComponentDefaultsAcceptance") / FPackageName::GetShortName(Root) / TEXT("snapshot.json");
    if (!TestTrue(TEXT("Reload oracle exists"), FFileHelper::LoadFileToString(Expected, *Oracle))) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("blueprint_path"), Root + TEXT("/BP_Grandchild")); FString Error; auto* Blueprint = Load(Params, Error);
    if (!TestNotNull(TEXT("Saved target loads"), Blueprint)) return false;
    TestEqual(TEXT("Complete source/default/ownership scope cold identical"), Json(Snapshot(Blueprint)), Expected);
    auto* Mesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/SKM_Alternate"))); auto* Anim = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/ABP_Test")));
    TArray<UObject*> Ready{Mesh, Anim}; FAssetCompilingManager::Get().FinishCompilationForObjects(Ready);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false); auto* Actor = World->SpawnActor<AActor>(Blueprint->GeneratedClass);
    if (TestNotNull(TEXT("Cold actual spawned actor"), Actor))
    {
        TInlineComponentArray<USkeletalMeshComponent*> Components; Actor->GetComponents(Components); TestEqual(TEXT("Cold component count"), Components.Num(), 3);
        for (auto* Component : Components) { TestEqual(TEXT("Cold spawned Mesh"), Component->GetSkeletalMeshAsset(), Mesh); TestEqual(TEXT("Cold spawned Anim Class"), Component->GetAnimClass(), Anim->GeneratedClass.Get()); }
        Actor->Destroy();
    }
    World->DestroyWorld(false); TestFalse(TEXT("Cold target stays clean"), Blueprint->GetPackage()->IsDirty()); return !HasAnyErrors();
}
#endif