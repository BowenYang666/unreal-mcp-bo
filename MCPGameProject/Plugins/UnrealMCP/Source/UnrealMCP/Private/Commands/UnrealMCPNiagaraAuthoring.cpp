#include "Commands/UnrealMCPNiagaraAuthoring.h"
#include "Commands/UnrealMCPAssetProperties.h"
#include "Commands/UnrealMCPNiagaraScalability.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraEffectType.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNode.h"
#include "NiagaraCommon.h"
#include "NiagaraParameterMapHistory.h"
#include "EdGraphSchema_Niagara.h"
#include "Materials/MaterialInterface.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace UnrealMCPNiagaraAuthoring
{
    namespace
    {
        struct FField { FProperty* Property; void* Address; };
        struct FTarget
        {
            UObject* Asset = nullptr;
            UObject* Owner = nullptr;
            UNiagaraSystem* System = nullptr;
            FNiagaraEmitterHandle* Handle = nullptr;
            UNiagaraScript* Script = nullptr;
            UNiagaraNodeFunctionCall* Module = nullptr;
            FString Kind;
            TMap<FString, FField> Fields;
            TSharedRef<FJsonObject> Identity = MakeShared<FJsonObject>();
        };
        UEdGraphPin* OverridePin(UNiagaraNodeFunctionCall* Module, const FName& AliasedName);
        TArray<FNiagaraVariable> Inputs(const FTarget& Target);
        UNiagaraNodeFunctionCall* DynamicSource(const FTarget& Target, UEdGraphPin* Pin, const FGuid& Wanted,
            TSet<UEdGraphNode*>& Visited, int32 Depth = 0);
        struct FStaged
        {
            FProperty* Property;
            void* Address;
            void* Original;
            FStaged(FField Field) : Property(Field.Property), Original(Field.Address)
            {
                Address = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
                Property->InitializeValue(Address);
                Property->CopyCompleteValue(Address, Original);
            }
            ~FStaged() { Property->DestroyValue(Address); FMemory::Free(Address); }
        };
        #if WITH_DEV_AUTOMATION_TESTS
            bool FailSaveForTest = false;
            bool FailCompileForTest = false;
        #endif

            bool Save(UObject* Asset)
            {
        #if WITH_DEV_AUTOMATION_TESTS
                if (FailSaveForTest) return false;
        #endif
                return UEditorAssetLibrary::SaveLoadedAsset(Asset, false) && !Asset->GetPackage()->IsDirty();
            }

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

        FString Json(const TSharedPtr<FJsonValue>& Value)
        {
            FString Text;
            FJsonSerializer::Serialize(Value, TEXT(""), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
            return Text;
        }

        bool Stored(const FProperty* Property)
        {
            return Property && Property->ArrayDim == 1 && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated);
        }

        TSharedPtr<FJsonValue> Encode(FProperty* Property, const void* Address, FString& Error, int32 Depth = 0)
        {
            if (Depth > 16) { Error = TEXT("Nested value exceeds 16 levels"); return nullptr; }
            if (auto* Struct = CastField<FStructProperty>(Property))
            {
                auto Result = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
                {
                    if (!Stored(*Field)) continue;
                    auto Value = Encode(*Field, Field->ContainerPtrToValuePtr<void>(Address), Error, Depth + 1);
                    if (!Value) return nullptr;
                    Result->SetField(Field->GetName(), Value);
                }
                return MakeShared<FJsonValueObject>(Result);
            }
            if (auto* Array = CastField<FArrayProperty>(Property))
            {
                FScriptArrayHelper Helper(Array, Address);
                if (Helper.Num() > 1024) { Error = TEXT("Array exceeds 1024 elements"); return nullptr; }
                TArray<TSharedPtr<FJsonValue>> Values;
                for (int32 Index = 0; Index < Helper.Num(); ++Index)
                {
                    auto Value = Encode(Array->Inner, Helper.GetRawPtr(Index), Error, Depth + 1);
                    if (!Value) return nullptr;
                    Values.Add(Value);
                }
                return MakeShared<FJsonValueArray>(Values);
            }
            return UnrealMCPAssetProperties::EncodeValue(Property, Address, Error);
        }

        bool Decode(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, FString& Error,
            bool Whole = false, int32 Depth = 0)
        {
            if (!Value || Depth > 16 || !Stored(Property) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_InstancedReference))
            { Error = TEXT("Unsupported or read-only saved field"); return false; }
            if (auto* Struct = CastField<FStructProperty>(Property))
            {
                if (Value->Type != EJson::Object) { Error = TEXT("Struct requires a typed object"); return false; }
                if (Whole) for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
                    if (Stored(*Field) && !Value->AsObject()->HasField(Field->GetName()))
                    { Error = TEXT("Array rows require every saved field: ") + Field->GetName(); return false; }
                TSet<FName> Seen;
                for (const auto& Pair : Value->AsObject()->Values)
                {
                    if (Seen.Contains(FName(*Pair.Key))) { Error = TEXT("Duplicate field after name conversion"); return false; }
                    Seen.Add(FName(*Pair.Key));
                    auto* Field = Struct->Struct->FindPropertyByName(FName(*Pair.Key));
                    if (!Field || !Decode(Field, Field->ContainerPtrToValuePtr<void>(Address), Pair.Value, Error, Whole, Depth + 1))
                    { Error = Pair.Key + TEXT(": ") + (Error.IsEmpty() ? TEXT("Unknown field") : Error); return false; }
                }
                return true;
            }
            if (auto* Array = CastField<FArrayProperty>(Property))
            {
                if (Value->Type != EJson::Array || Value->AsArray().Num() > 1024)
                { Error = TEXT("Array requires at most 1024 complete typed rows"); return false; }
                FScriptArrayHelper Helper(Array, Address);
                Helper.EmptyValues(); Helper.AddValues(Value->AsArray().Num());
                for (int32 Index = 0; Index < Helper.Num(); ++Index)
                    if (!Decode(Array->Inner, Helper.GetRawPtr(Index), Value->AsArray()[Index], Error, true, Depth + 1))
                    { Error = FString::Printf(TEXT("[%d]: "), Index) + Error; return false; }
                return true;
            }
            return UnrealMCPAssetProperties::DecodeValue(Property, Address, Value, Error);
        }

        bool Guard(FString& Error)
        {
            if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GIsSavingPackage || IsGarbageCollecting() || GIsSlowTask)
            { Error = TEXT("Authoring requires an idle Editor GameThread, outside PIE/save/GC/slow tasks"); return false; }
            return true;
        }

        TSharedPtr<FJsonObject> UserParameter(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable)
        {
            auto Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("name"), Variable.GetName().ToString());
            Result->SetStringField(TEXT("type"), Variable.GetType().GetName());
            const auto* Data = Store.GetParameterData(Variable);
            if (Data) Result->SetStringField(TEXT("saved_bytes"), BytesToHex(Data, Variable.GetSizeInBytes()));
            if (Variable.GetType() == FNiagaraTypeDefinition::GetFloatDef()) Result->SetNumberField(TEXT("value"), Store.GetParameterValue<float>(Variable));
            else if (Variable.GetType() == FNiagaraTypeDefinition::GetIntDef()) Result->SetNumberField(TEXT("value"), Store.GetParameterValue<int32>(Variable));
            else if (Variable.GetType() == FNiagaraTypeDefinition::GetBoolDef()) Result->SetBoolField(TEXT("value"), Store.GetParameterValue<FNiagaraBool>(Variable).GetValue());
            else if (Variable.GetType() == FNiagaraTypeDefinition::GetColorDef())
            {
                const auto Color = Store.GetParameterValue<FLinearColor>(Variable);
                Result->SetArrayField(TEXT("value"), {MakeShared<FJsonValueNumber>(Color.R), MakeShared<FJsonValueNumber>(Color.G),
                    MakeShared<FJsonValueNumber>(Color.B), MakeShared<FJsonValueNumber>(Color.A)});
            }
            else Result->SetStringField(TEXT("value_state"), TEXT("unsupported_for_edit_preserved"));
            return Result;
        }

        bool UserValue(const TSharedPtr<FJsonObject>& Entry, FNiagaraVariable& Variable, FString& Error)
        {
            FString Name, Type;
            if (!Entry || Entry->Values.Num() != 3 || !Entry->TryGetStringField(TEXT("name"), Name)
                || !Entry->TryGetStringField(TEXT("type"), Type) || !Entry->HasField(TEXT("value"))
                || !Name.StartsWith(TEXT("User.")) || Name.Len() <= 5 || Name.Len() >= NAME_SIZE
                || Name.TrimStartAndEnd() != Name || Name.Contains(TEXT("\n")) || Name.Contains(TEXT("\r")))
            { Error = TEXT("Each parameter requires exact User.name, type and value"); return false; }
            const auto Value = Entry->Values[TEXT("value")];
            if (Type == TEXT("float"))
            {
                if (Value->Type != EJson::Number || !FMath::IsFinite(Value->AsNumber()) || !FMath::IsFinite(static_cast<float>(Value->AsNumber()))
                    || (Value->AsNumber() != 0 && static_cast<float>(Value->AsNumber()) == 0))
                { Error = TEXT("Float requires a finite float-range number"); return false; }
                Variable = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), FName(*Name)); Variable.SetValue(static_cast<float>(Value->AsNumber()));
            }
            else if (Type == TEXT("int"))
            {
                if (Value->Type != EJson::Number || !FMath::IsFinite(Value->AsNumber()) || Value->AsNumber() < MIN_int32
                    || Value->AsNumber() > MAX_int32 || FMath::FloorToDouble(Value->AsNumber()) != Value->AsNumber())
                { Error = TEXT("Int requires an integral int32 number"); return false; }
                Variable = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), FName(*Name)); Variable.SetValue(static_cast<int32>(Value->AsNumber()));
            }
            else if (Type == TEXT("bool"))
            {
                if (Value->Type != EJson::Boolean) { Error = TEXT("Bool requires a JSON boolean"); return false; }
                Variable = FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), FName(*Name)); Variable.SetValue(Value->AsBool());
            }
            else if (Type == TEXT("color"))
            {
                if (Value->Type != EJson::Array || Value->AsArray().Num() != 4) { Error = TEXT("Color requires [r,g,b,a] linear HDR values"); return false; }
                float Channels[4];
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    const auto Component = Value->AsArray()[Index];
                    if (Component->Type != EJson::Number || !FMath::IsFinite(Component->AsNumber()) || !FMath::IsFinite(static_cast<float>(Component->AsNumber()))
                        || (Component->AsNumber() != 0 && static_cast<float>(Component->AsNumber()) == 0))
                    { Error = TEXT("Color channels must be finite float-range numbers"); return false; }
                    Channels[Index] = static_cast<float>(Component->AsNumber());
                }
                Variable = FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), FName(*Name));
                Variable.SetValue(FLinearColor(Channels[0], Channels[1], Channels[2], Channels[3]));
            }
            else { Error = TEXT("Supported User types: float, int, bool, color; existing other types are preserved"); return false; }
            return true;
        }

        bool Resolve(const TSharedPtr<FJsonObject>& Params, FTarget& Target, FString& Error)
        {
            FString Path;
            if (!Params || !Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
                || Path.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Path))
            { Error = TEXT("Require an exact /Game package asset_path, not an object path or short name"); return false; }
            Target.Asset = UEditorAssetLibrary::LoadAsset(Path);
            if (!Target.Asset) { Error = TEXT("Asset not found: ") + Path; return false; }
            Target.Owner = Target.Asset;
            Params->TryGetStringField(TEXT("target"), Target.Kind);
            if (Target.Kind.IsEmpty()) Target.Kind = Cast<UNiagaraEffectType>(Target.Asset) ? TEXT("effect_type") : TEXT("system");
            const UStruct* Type = nullptr;
            void* Address = nullptr;
            TArray<FName> Names;
            Target.Identity->SetStringField(TEXT("asset_path"), Path);
            Target.Identity->SetStringField(TEXT("target"), Target.Kind);
            if (Target.Kind == TEXT("effect_type") && Cast<UNiagaraEffectType>(Target.Asset))
            {
                Type = Target.Asset->GetClass(); Address = Target.Asset;
                Names = {TEXT("UpdateFrequency"), TEXT("CullReaction"), TEXT("bAllowCullingForLocalPlayers"),
                    TEXT("SystemScalabilitySettings"), TEXT("EmitterScalabilitySettings")};
            }
            else if ((Target.System = Cast<UNiagaraSystem>(Target.Asset)))
            {
                if (Target.Kind == TEXT("system"))
                {
                    Type = Target.System->GetClass(); Address = Target.System;
                    Names = {TEXT("EffectType"), TEXT("Platforms"), TEXT("bOverrideScalabilitySettings"),
                        TEXT("SystemScalabilityOverrides"), TEXT("bOverrideAllowCullingForLocalPlayers"), TEXT("bAllowCullingForLocalPlayersOverride")};
                }
                else if (Target.Kind == TEXT("emitter") || Target.Kind == TEXT("renderer") || Target.Kind == TEXT("module") || Target.Kind == TEXT("dynamic_input"))
                {
                    FString HandleId;
                    if (!Params->TryGetStringField(TEXT("emitter_handle_id"), HandleId))
                    { Error = TEXT("Require emitter_handle_id from the reader"); return false; }
                    FGuid HandleGuid;
                    if (!FGuid::Parse(HandleId, HandleGuid)) { Error = TEXT("Invalid emitter_handle_id"); return false; }
                    FNiagaraEmitterHandle* Handle = nullptr;
                    for (auto& Candidate : Target.System->GetEmitterHandles()) if (Candidate.GetId() == HandleGuid) Handle = &Candidate;
                    if (!Handle || Handle->GetEmitterMode() == ENiagaraEmitterMode::Stateless || !Handle->GetEmitterData())
                    { Error = TEXT("Missing selected emitter data or unsupported Stateless target"); return false; }
                    auto* Data = Handle->GetEmitterData();
                    Target.Handle = Handle;
                    Target.Owner = Handle->GetInstance().Emitter;
                    Target.Identity->SetStringField(TEXT("emitter_handle_id"), HandleId);
                    Target.Identity->SetStringField(TEXT("selected_version_guid"), Handle->GetInstance().Version.ToString(EGuidFormats::DigitsWithHyphens));
                    Target.Identity->SetStringField(TEXT("resolved_version_guid"), Data->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
                    if (Target.Kind == TEXT("emitter"))
                    {
                        Type = FVersionedNiagaraEmitterData::StaticStruct(); Address = Data;
                        Names = {TEXT("Platforms"), TEXT("ScalabilityOverrides")};
                    }
                    else if (Target.Kind == TEXT("renderer"))
                    {
                        double Slot;
                        if (!Params->TryGetNumberField(TEXT("renderer_index"), Slot) || Slot < 0 || Slot > MAX_int32
                            || FMath::FloorToDouble(Slot) != Slot || !Data->GetRenderers().IsValidIndex(static_cast<int32>(Slot)))
                        { Error = TEXT("Require a valid integral renderer_index from the source slots"); return false; }
                        Target.Owner = const_cast<UNiagaraRendererProperties*>(Data->GetRenderers()[static_cast<int32>(Slot)]);
                        if (!Target.Owner) { Error = TEXT("Renderer slot is null"); return false; }
                        Type = Target.Owner->GetClass(); Address = Target.Owner; Names = {TEXT("Platforms")};
                        Target.Identity->SetNumberField(TEXT("renderer_index"), Slot);
                    }
                    else
                    {
                        FString ScriptType, NodeId;
                        Params->TryGetStringField(TEXT("script_type"), ScriptType);
                        if (ScriptType == TEXT("spawn")) Target.Script = Data->SpawnScriptProps.Script;
                        else if (ScriptType == TEXT("update")) Target.Script = Data->UpdateScriptProps.Script;
                        else if (ScriptType == TEXT("emitter_spawn")) Target.Script = Data->EmitterSpawnScriptProps.Script;
                        else if (ScriptType == TEXT("emitter_update")) Target.Script = Data->EmitterUpdateScriptProps.Script;
                        FGuid NodeGuid;
                        if (!Target.Script || !Params->TryGetStringField(TEXT("module_node_guid"), NodeId) || !FGuid::Parse(NodeId, NodeGuid))
                        { Error = TEXT("Module requires exact script_type and module_node_guid"); return false; }
                        auto* Source = Cast<UNiagaraScriptSource>(Target.Script->GetSource(FGuid()));
                        if (!Source || !Source->NodeGraph) { Error = TEXT("Selected script graph unavailable"); return false; }
                        for (UEdGraphNode* Node : Source->NodeGraph->Nodes) if (Node && Node->NodeGuid == NodeGuid)
                        {
                            if (Target.Module) { Error = TEXT("Duplicate node GUID in graph"); return false; }
                            Target.Module = Cast<UNiagaraNodeFunctionCall>(Node);
                        }
                        if (!Target.Module || !Target.Module->FunctionScript || !Target.Module->FunctionScript->GetScriptData(Target.Module->SelectedScriptVersion)
                            || Target.Module->FunctionScript->GetUsage() != ENiagaraScriptUsage::Module)
                        { Error = TEXT("Missing module/selected version; dynamic functions are not module targets"); return false; }
                        Target.Owner = Target.Module; Type = Target.Owner->GetClass(); Address = Target.Owner;
                        Target.Identity->SetStringField(TEXT("script_type"), ScriptType);
                        Target.Identity->SetStringField(TEXT("module_node_guid"), NodeId);
                        Target.Identity->SetStringField(TEXT("module_selected_version_guid"), Target.Module->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens));
                        Target.Identity->SetStringField(TEXT("script_asset_path"), Target.Module->FunctionScript->GetPathName());
                        Target.Identity->SetStringField(TEXT("script_change_id"), Target.Module->FunctionScript->GetBaseChangeID(Target.Module->SelectedScriptVersion).ToString());
                        Target.Identity->SetStringField(TEXT("graph_path"), Source->NodeGraph->GetPathName());
                        if (Target.Kind == TEXT("dynamic_input"))
                        {
                            FString ParentInput, FunctionId;
                            FGuid FunctionGuid;
                            if (!Params->TryGetStringField(TEXT("parent_input_name"), ParentInput)
                                || !Params->TryGetStringField(TEXT("function_node_guid"), FunctionId) || !FGuid::Parse(FunctionId, FunctionGuid))
                            { Error = TEXT("dynamic_input requires parent_input_name and exact function_node_guid"); return false; }
                            FNiagaraVariable ParentVariable;
                            for (const auto& Input : Inputs(Target)) if (Input.GetName().ToString() == ParentInput) ParentVariable = Input;
                            if (!ParentVariable.IsValid()) { Error = TEXT("Parent canonical input unavailable"); return false; }
                            const auto Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(ParentVariable.GetName()), Target.Module);
                            auto* Pin = OverridePin(Target.Module, Aliased.GetParameterHandleString());
                            TSet<UEdGraphNode*> Visited;
                            auto* Function = DynamicSource(Target, Pin, FunctionGuid, Visited);
                            if (!Function || !Function->FunctionScript || !Function->FunctionScript->GetScriptData(Function->SelectedScriptVersion))
                            { Error = TEXT("Dynamic function/version not found in the selected parent input chain; no name or latest-version fallback"); return false; }
                            Target.Identity->SetStringField(TEXT("parent_module_path"), Target.Module->GetPathName());
                            Target.Identity->SetStringField(TEXT("parent_input_name"), ParentInput);
                            Target.Identity->SetStringField(TEXT("function_node_guid"), FunctionId);
                            Target.Identity->SetStringField(TEXT("function_selected_version_guid"), Function->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens));
                            Target.Identity->SetStringField(TEXT("function_resolved_version_guid"), Function->FunctionScript->GetScriptData(Function->SelectedScriptVersion)->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
                            Target.Identity->SetStringField(TEXT("function_script_path"), Function->FunctionScript->GetPathName());
                            Target.Identity->SetStringField(TEXT("function_change_id"), Function->FunctionScript->GetBaseChangeID(Function->SelectedScriptVersion).ToString());
                            Target.Module = Function; Target.Owner = Function; Type = Function->GetClass(); Address = Function;
                        }
                    }
                    if (!Target.Owner || Target.Owner->GetPackage() != Target.Asset->GetPackage())
                    { Error = TEXT("Only system-owned emitter/renderer data may be edited"); return false; }
                }
            }
            if (!Type || !Address) { Error = TEXT("Target must be Niagara system/effect_type/emitter/renderer"); return false; }
            Target.Identity->SetStringField(TEXT("object_path"), Target.Owner->GetPathName());
            for (FName Name : Names)
            {
                auto* Property = Type->FindPropertyByName(Name);
                if (!Property) { Error = TEXT("Field unavailable in this engine: ") + Name.ToString(); return false; }
                Target.Fields.Add(Name.ToString(), {Property, Property->ContainerPtrToValuePtr<void>(Address)});
            }
            return true;
        }

        UEdGraphPin* OverridePin(UNiagaraNodeFunctionCall* Module, const FName& AliasedName)
        {
            for (auto* Input : Module->Pins)
            {
                if (!Input || Input->Direction != EGPD_Input || Input->LinkedTo.Num() != 1) continue;
                auto* Node = Input->LinkedTo[0]->GetOwningNode();
                if (!Node || Node->GetClass()->GetName() != TEXT("NiagaraNodeParameterMapSet")) continue;
                for (auto* Pin : Node->Pins) if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == AliasedName) return Pin;
            }
            return nullptr;
        }

        TArray<FNiagaraVariable> Inputs(const FTarget& Target)
        {
            TArray<FNiagaraVariable> Values; TSet<FNiagaraVariable> Hidden;
            FCompileConstantResolver Resolver(Target.Handle->GetInstance(), Target.Script->GetUsage());
            FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Target.Module, Values, Hidden, Resolver,
                Target.Module->FunctionScript && Target.Module->FunctionScript->GetUsage() == ENiagaraScriptUsage::DynamicInput
                    ? FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::AllInputs
                    : FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);
            return Values;
        }

        UNiagaraNodeFunctionCall* DynamicSource(const FTarget& Target, UEdGraphPin* Pin, const FGuid& Wanted,
            TSet<UEdGraphNode*>& Visited, int32 Depth)
        {
            if (!Pin || Pin->LinkedTo.Num() != 1 || Depth > 16 || Visited.Num() >= 64) return nullptr;
            auto* Node = Pin->LinkedTo[0]->GetOwningNode();
            if (!Node || Node->GetGraph() != Target.Module->GetGraph() || Node->GetClass()->GetName() == TEXT("NiagaraNodeCustomHlsl") || Visited.Contains(Node)) return nullptr;
            Visited.Add(Node);
            auto* Function = Cast<UNiagaraNodeFunctionCall>(Node);
            if (!Function || !Function->FunctionScript || Function->FunctionScript->GetUsage() != ENiagaraScriptUsage::DynamicInput
                || !Function->FunctionScript->GetScriptData(Function->SelectedScriptVersion)) return nullptr;
            if (Function->NodeGuid == Wanted) return Function;
            FTarget Child = Target; Child.Module = Function;
            for (const auto& Input : Inputs(Child))
            {
                const auto Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input.GetName()), Function);
                if (auto* Found = DynamicSource(Child, OverridePin(Function, Aliased.GetParameterHandleString()), Wanted, Visited, Depth + 1)) return Found;
            }
            return nullptr;
        }

        TSharedPtr<FJsonObject> ModuleSnapshot(const FTarget& Target)
        {
            auto Result = MakeShared<FJsonObject>();
            auto* Graph = Target.Module->GetGraph();
            TArray<TSharedPtr<FJsonValue>> Nodes;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node) continue;
                auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("path"), Node->GetPathName());
                Item->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
                if (const auto* Function = Cast<UNiagaraNodeFunctionCall>(Node))
                {
                    Item->SetStringField(TEXT("selected_version_guid"), Function->SelectedScriptVersion.ToString());
                    if (Function->FunctionScript)
                    {
                        Item->SetStringField(TEXT("script_path"), Function->FunctionScript->GetPathName());
                        Item->SetStringField(TEXT("script_change_id"), Function->FunctionScript->GetBaseChangeID(Function->SelectedScriptVersion).ToString());
                    }
                }
                TArray<TSharedPtr<FJsonValue>> Pins;
                for (const auto* Pin : Node->Pins)
                {
                    if (!Pin) continue;
                    auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    Entry->SetStringField(TEXT("id"), Pin->PinId.ToString()); Entry->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
                    Entry->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
                    Entry->SetStringField(TEXT("default"), Pin->DefaultValue); Entry->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
                    if (Pin->DefaultObject) Entry->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
                    TArray<TSharedPtr<FJsonValue>> Links;
                    for (const auto* Linked : Pin->LinkedTo) if (Linked && Linked->GetOwningNode())
                        Links.Add(MakeShared<FJsonValueString>(Linked->GetOwningNode()->GetPathName() + TEXT(":") + Linked->PinId.ToString()));
                    Entry->SetArrayField(TEXT("links"), Links); Pins.Add(MakeShared<FJsonValueObject>(Entry));
                }
                Item->SetArrayField(TEXT("pins"), Pins); Nodes.Add(MakeShared<FJsonValueObject>(Item));
            }
            Result->SetArrayField(TEXT("graph_nodes"), Nodes);
            TArray<TSharedPtr<FJsonValue>> Bindings;
            for (const auto& Input : Inputs(Target))
            {
                auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"), Input.GetName().ToString());
                Item->SetStringField(TEXT("type"), Input.GetType().GetName()); Item->SetBoolField(TEXT("is_static"), Input.GetType().IsStatic());
                const auto Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input.GetName()), Target.Module);
                auto* Pin = OverridePin(Target.Module, Aliased.GetParameterHandleString());
                Item->SetStringField(TEXT("mode"), Pin && !Pin->LinkedTo.IsEmpty() ? TEXT("Override") : TEXT("DefaultOrLocal"));
                if (Pin && Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0]->GetOwningNode())
                {
                    const auto* Source = Pin->LinkedTo[0];
                    Item->SetStringField(TEXT("source_node_guid"), Source->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
                    Item->SetStringField(TEXT("source_pin_id"), Source->PinId.ToString(EGuidFormats::DigitsWithHyphens));
                    Item->SetStringField(TEXT("source_node_path"), Source->GetOwningNode()->GetPathName());
                    if (const auto* Function = Cast<UNiagaraNodeFunctionCall>(Source->GetOwningNode()))
                    {
                        Item->SetStringField(TEXT("source_selected_version_guid"), Function->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens));
                        if (Function->FunctionScript) Item->SetStringField(TEXT("source_script_path"), Function->FunctionScript->GetPathName());
                    }
                }
                if (Pin && Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
                { Item->SetStringField(TEXT("mode"), TEXT("Linked")); Item->SetStringField(TEXT("linked_parameter"), Pin->LinkedTo[0]->PinName.ToString()); }
                Bindings.Add(MakeShared<FJsonValueObject>(Item));
            }
            Result->SetArrayField(TEXT("inputs"), Bindings);
            TArray<FNiagaraVariable> Rapid; Target.Script->RapidIterationParameters.GetParameters(Rapid);
            TArray<TSharedPtr<FJsonValue>> Constants;
            for (const auto& Variable : Rapid) Constants.Add(MakeShared<FJsonValueObject>(UserParameter(Target.Script->RapidIterationParameters, Variable)));
            Result->SetArrayField(TEXT("rapid_parameters"), Constants);
            return Result;
        }

        TSharedPtr<FJsonObject> Snapshot(const FTarget& Target, FString& Error)
        {
            auto Result = MakeShared<FJsonObject>();
            for (const auto& Pair : Target.Identity->Values) Result->SetField(Pair.Key, Pair.Value);
            if (const auto* Effect = Cast<UNiagaraEffectType>(Target.Asset))
            {
                const auto* Handler = Effect->GetSignificanceHandler();
                Result->SetStringField(TEXT("significance_handler"), !Handler ? TEXT("None")
                    : Handler->IsA<UNiagaraSignificanceHandlerAge>() ? TEXT("Age")
                    : Handler->IsA<UNiagaraSignificanceHandlerDistance>() ? TEXT("Distance") : Handler->GetClass()->GetPathName());
                if (Handler) Result->SetStringField(TEXT("significance_object_path"), Handler->GetPathName());
            }
            auto Fields = MakeShared<FJsonObject>();
            TArray<FString> Names; Target.Fields.GetKeys(Names); Names.Sort();
            for (const FString& Name : Names)
            {
                const auto& Field = Target.Fields[Name];
                auto Value = Encode(Field.Property, Field.Address, Error);
                if (!Value) { Error = Name + TEXT(": ") + Error; return nullptr; }
                Fields->SetField(Name, Value);
            }
            Result->SetObjectField(TEXT("fields"), Fields);
            if (Target.Kind == TEXT("renderer")) Result->SetObjectField(TEXT("material_parameters"), ReadMaterialParameters(Cast<UNiagaraRendererProperties>(Target.Owner)));
            if (Target.Module) Result->SetObjectField(TEXT("module"), ModuleSnapshot(Target));
            if (Target.Module)
            {
                auto* Multiply = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(TEXT("/Niagara/DynamicInputs/Multiply/Multiply_Float")));
                if (Multiply && Multiply->GetScriptData(FGuid()))
                {
                    auto Metadata = MakeShared<FJsonObject>(); Metadata->SetStringField(TEXT("asset_path"), Multiply->GetPathName());
                    Metadata->SetStringField(TEXT("version_guid"), Multiply->GetScriptData(FGuid())->Version.VersionGuid.ToString(EGuidFormats::DigitsWithHyphens));
                    Result->SetObjectField(TEXT("standard_float_multiplier"), Metadata);
                }
            }
            if (Target.System)
            {
                TArray<FNiagaraVariable> Variables; Target.System->GetExposedParameters().GetParameters(Variables);
                Variables.Sort([](const FNiagaraVariable& Left, const FNiagaraVariable& Right) { return Left.GetName().LexicalLess(Right.GetName()); });
                TArray<TSharedPtr<FJsonValue>> Parameters;
                for (const auto& Variable : Variables) Parameters.Add(MakeShared<FJsonValueObject>(UserParameter(Target.System->GetExposedParameters(), Variable)));
                Result->SetArrayField(TEXT("user_parameters"), Parameters);
            }
            const auto Utf8 = FTCHARToUTF8(*Json(MakeShared<FJsonValueObject>(Result)));
            FSHAHash Digest; FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest.Hash);
            Result->SetStringField(TEXT("baseline"), Digest.ToString());
            Result->SetNumberField(TEXT("authoring_contract"), 1);
            Result->SetBoolField(TEXT("supports_simple_user_redirect"), true);
            Result->SetBoolField(TEXT("supports_dynamic_input_targets"), true);
            Result->SetBoolField(TEXT("package_dirty"), Target.Asset->GetPackage()->IsDirty());
            Result->SetStringField(TEXT("semantics"), TEXT("Saved fields only; nested object patches preserve unspecified fields; arrays replace complete rows. Baseline covers target identity and fields, not runtime eligibility."));
            return Result;
        }

        bool Baseline(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonObject>& Before, FString& Error)
        {
            FString Expected;
            if (!Params->TryGetStringField(TEXT("expected_baseline"), Expected) || Expected != Before->GetStringField(TEXT("baseline")))
            { Error = TEXT("Missing/stale expected_baseline; read this exact target again"); return false; }
            return true;
        }

        TSharedPtr<FJsonObject> Finish(const TSharedPtr<FJsonObject>& Params, FTarget& Target,
            const TSharedPtr<FJsonObject>& Before, bool Modified, bool Save)
        {
            auto Result = MakeShared<FJsonObject>();
            Result->SetNumberField(TEXT("authoring_contract"), 1);
            Result->SetBoolField(TEXT("modified"), Modified); Result->SetBoolField(TEXT("saved"), false);
            Result->SetBoolField(TEXT("compiled"), false); Result->SetBoolField(TEXT("compile_required"), Target.System && (Modified || Save));
            Result->SetObjectField(TEXT("before"), Before);
            FString Error;
            if (Target.System && (Modified || Save))
            {
                Target.System->RequestCompile(true); Target.System->WaitForCompilationComplete();
                bool Compiled = Target.System->IsValid() && Target.System->IsReadyToRun();
                TArray<UNiagaraScript*> Scripts = {Target.System->GetSystemSpawnScript(), Target.System->GetSystemUpdateScript()};
                for (const auto& Handle : Target.System->GetEmitterHandles()) if (const auto* Data = Handle.GetEmitterData())
                {
                    Data->GetScripts(Scripts);
                }
                TArray<TSharedPtr<FJsonValue>> Reports;
                for (const auto* Script : Scripts) if (Script)
                {
                    auto Report = MakeShared<FJsonObject>(); Report->SetStringField(TEXT("script"), Script->GetPathName());
                    Report->SetNumberField(TEXT("status_code"), static_cast<int32>(Script->GetLastCompileStatus()));
                    TArray<TSharedPtr<FJsonValue>> Events;
                    for (const auto& Event : Script->GetVMExecutableData().LastCompileEvents)
                    {
                        auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("message"), Event.Message);
                        Entry->SetNumberField(TEXT("severity"), static_cast<int32>(Event.Severity)); Events.Add(MakeShared<FJsonValueObject>(Entry));
                    }
                    Report->SetArrayField(TEXT("events"), Events); Reports.Add(MakeShared<FJsonValueObject>(Report));
                }
                Result->SetArrayField(TEXT("compile_reports"), Reports);
#if WITH_DEV_AUTOMATION_TESTS
                Compiled &= !FailCompileForTest;
#endif
                Result->SetBoolField(TEXT("compiled"), Compiled);
                if (!Compiled) { Error = TEXT("Niagara compilation is invalid or not ready; asset remains modified and unsaved"); Result->SetStringField(TEXT("stage"), TEXT("compile")); }
            }
            if (Error.IsEmpty() && Save)
            {
                const bool Saved = UnrealMCPNiagaraAuthoring::Save(Target.Asset);
                Result->SetBoolField(TEXT("saved"), Saved);
                if (!Saved) { Error = TEXT("Save failed; inspect dirty state and actual after; no rollback or retry"); Result->SetStringField(TEXT("stage"), TEXT("save")); }
            }
            FTarget Actual; FString ReadError;
            const auto After = Resolve(Params, Actual, ReadError) ? Snapshot(Actual, ReadError) : nullptr;
            if (After) Result->SetObjectField(TEXT("after"), After);
            else if (Error.IsEmpty()) { Error = TEXT("Readback failed: ") + ReadError; Result->SetStringField(TEXT("stage"), TEXT("readback")); }
            Result->SetBoolField(TEXT("package_dirty"), Target.Asset->GetPackage()->IsDirty());
            Result->SetBoolField(TEXT("success"), Error.IsEmpty());
            if (!Error.IsEmpty()) Result->SetStringField(TEXT("error"), Error);
            return Result;
        }
    }

    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params)
    {
        FTarget Target; FString Error;
        if (!Resolve(Params, Target, Error)) return Failure(Error);
        auto Result = Snapshot(Target, Error);
        if (!Result) return Failure(Error);
        Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        return Result;
    }

    TSharedPtr<FJsonObject> CreateEffectType(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error, Path;
        if (!Guard(Error)) return Failure(Error);
        if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/Game/"))
            || Path.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Path)) return Failure(TEXT("Require a new exact /Game package path"));
        if (FindPackage(nullptr, *Path) || FPackageName::DoesPackageExist(Path)) return Failure(TEXT("Target package already exists; never overwrite"));
        bool DryRun = true, Save = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be bool"));
        auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("authoring_contract"), 1);
        Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("created"), false);
        Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("dry_run"), DryRun); Result->SetStringField(TEXT("asset_path"), Path);
        if (DryRun) return Result;
        auto* Package = CreatePackage(*Path);
        auto* Asset = NewObject<UNiagaraEffectType>(Package, FName(*FPackageName::GetLongPackageAssetName(Path)), RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Asset); Asset->MarkPackageDirty();
        Result->SetBoolField(TEXT("created"), true); Result->SetBoolField(TEXT("modified"), true);
        const bool Saved = Save && UnrealMCPNiagaraAuthoring::Save(Asset);
        Result->SetBoolField(TEXT("saved"), Saved); Result->SetBoolField(TEXT("package_dirty"), Package->IsDirty());
        if (Save && !Saved) { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("save")); Result->SetStringField(TEXT("error"), TEXT("Created in memory but save failed")); }
        return Result;
    }

    TSharedPtr<FJsonObject> WriteScalability(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; FTarget Target;
        if (!Guard(Error) || !Resolve(Params, Target, Error)) return Failure(Error);
        auto Before = Snapshot(Target, Error);
        if (!Before || !Baseline(Params, Before, Error)) return Failure(Error);
        bool DryRun = true, Save = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be bool"));
        if (Target.Asset->GetPackage()->IsDirty()) return Failure(TEXT("Preexisting dirty asset refused; explicitly save the approved draft first"));
        FString Significance;
        if (Params->HasField(TEXT("significance_handler")) && !Params->TryGetStringField(TEXT("significance_handler"), Significance))
            return Failure(TEXT("significance_handler must be a string"));
        if (!Significance.IsEmpty() && (Target.Kind != TEXT("effect_type")
            || (Significance != TEXT("None") && Significance != TEXT("Age") && Significance != TEXT("Distance"))))
            return Failure(TEXT("Significance selection is effect_type-only: Age, Distance or None; empty preserves existing handler"));
        const TArray<TSharedPtr<FJsonValue>>* Changes = nullptr;
        if (!Params->TryGetArrayField(TEXT("changes"), Changes) || (Changes->IsEmpty() && Significance.IsEmpty()) || Changes->Num() > 64)
            return Failure(TEXT("changes requires 1..64 {field,value} objects; structs merge and arrays replace complete rows"));
        TMap<FString, TUniquePtr<FStaged>> Staged;
        for (const auto& Value : *Changes)
        {
            FString Name;
            if (Value->Type != EJson::Object || Value->AsObject()->Values.Num() != 2 || !Value->AsObject()->TryGetStringField(TEXT("field"), Name)
                || !Value->AsObject()->HasField(TEXT("value")) || !Target.Fields.Contains(Name) || Staged.Contains(Name))
                return Failure(TEXT("Unknown/duplicate field or invalid change; only this target's saved fields may be patched"));
            auto Proposal = MakeUnique<FStaged>(Target.Fields[Name]);
            if (!Decode(Proposal->Property, Proposal->Address, Value->AsObject()->Values[TEXT("value")], Error)) return Failure(Name + TEXT(": ") + Error);
            Staged.Add(Name, MoveTemp(Proposal));
        }
        const bool ChangeSignificance = !Significance.IsEmpty() && Significance != Before->GetStringField(TEXT("significance_handler"));
        bool Modified = ChangeSignificance;
        auto Proposed = MakeShared<FJsonObject>();
        for (const auto& Pair : Staged)
        {
            auto Value = Encode(Pair.Value->Property, Pair.Value->Address, Error);
            if (!Value) return Failure(Pair.Key + TEXT(": ") + Error);
            Proposed->SetField(Pair.Key, Value);
            Modified |= Json(Value) != Json(Before->GetObjectField(TEXT("fields"))->Values[Pair.Key]);
        }
        if (DryRun)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("authoring_contract"), 1);
            Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("dry_run"), true);
            Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
            Result->SetBoolField(TEXT("would_modify"), Modified); Result->SetObjectField(TEXT("before"), Before);
            if (!Significance.IsEmpty()) Result->SetStringField(TEXT("proposed_significance_handler"), Significance);
            Result->SetObjectField(TEXT("proposed_fields"), Proposed); return Result;
        }
        if (Modified)
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "NiagaraAuthoring", "Edit Niagara saved scalability"));
            Target.Asset->Modify(); if (Target.Owner != Target.Asset) Target.Owner->Modify();
            if (ChangeSignificance)
            {
                auto* Property = FindFProperty<FObjectPropertyBase>(Target.Asset->GetClass(), TEXT("SignificanceHandler"));
                UObject* Handler = Significance == TEXT("None") ? nullptr : Significance == TEXT("Age")
                    ? static_cast<UObject*>(NewObject<UNiagaraSignificanceHandlerAge>(Target.Asset, NAME_None, RF_Transactional))
                    : static_cast<UObject*>(NewObject<UNiagaraSignificanceHandlerDistance>(Target.Asset, NAME_None, RF_Transactional));
                Property->SetObjectPropertyValue_InContainer(Target.Asset, Handler);
            }
            for (const auto& Pair : Staged) Pair.Value->Property->CopyCompleteValue(Pair.Value->Original, Pair.Value->Address);
            Target.Owner->PostEditChange(); if (Target.Owner != Target.Asset) Target.Asset->PostEditChange();
            Target.Asset->MarkPackageDirty();
        }
        return Finish(Params, Target, Before, Modified, Save);
    }

    TSharedPtr<FJsonObject> SaveDraft(const TSharedPtr<FJsonObject>& Params)
    {
        FString Error; FTarget Target;
        if (!Guard(Error) || !Resolve(Params, Target, Error)) return Failure(Error);
        auto Before = Snapshot(Target, Error);
        if (!Before || !Baseline(Params, Before, Error)) return Failure(Error);
        return Finish(Params, Target, Before, false, true);
    }

    TSharedPtr<FJsonObject> SetUserParameters(const TSharedPtr<FJsonObject>& Params)
    {
        FTarget Target; FString Error;
        if (!Guard(Error) || !Resolve(Params, Target, Error)) return Failure(Error);
        if (!Target.System || Target.Kind != TEXT("system")) return Failure(TEXT("User declarations require target=system"));
        auto Before = Snapshot(Target, Error);
        if (!Before || !Baseline(Params, Before, Error)) return Failure(Error);
        bool DryRun = true, Save = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be bool"));
        if (Target.Asset->GetPackage()->IsDirty()) return Failure(TEXT("Preexisting dirty asset refused"));
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Params->TryGetArrayField(TEXT("parameters"), Entries) || Entries->IsEmpty() || Entries->Num() > 64)
            return Failure(TEXT("parameters requires 1..64 name/type/value declarations"));
        auto& Store = Target.System->GetExposedParameters();
        TArray<FNiagaraVariable> Existing; Store.GetParameters(Existing);
        TSet<FName> Names; TArray<FNiagaraVariable> Proposed;
        for (const auto& Entry : *Entries)
        {
            FNiagaraVariable Variable;
            if (Entry->Type != EJson::Object || !UserValue(Entry->AsObject(), Variable, Error)) return Failure(Error);
            if (Names.Contains(Variable.GetName())) return Failure(TEXT("Duplicate User parameter name"));
            Names.Add(Variable.GetName());
            for (const auto& Old : Existing) if (Old.GetName() == Variable.GetName() && Old.GetType() != Variable.GetType())
                return Failure(TEXT("Existing User name has a different type; no implicit type migration"));
            const auto* OldData = Store.GetParameterData(Variable);
            if (!OldData || FMemory::Memcmp(OldData, Variable.GetData(), Variable.GetSizeInBytes()) != 0) Proposed.Add(Variable);
        }
        if (DryRun)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("authoring_contract"), 1);
            Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
            Result->SetBoolField(TEXT("would_modify"), !Proposed.IsEmpty()); Result->SetObjectField(TEXT("before"), Before);
            Result->SetArrayField(TEXT("proposed_parameters"), *Entries); return Result;
        }
        if (!Proposed.IsEmpty())
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "NiagaraUserParameters", "Edit Niagara User declarations"));
            Target.System->Modify();
            for (const auto& Variable : Proposed) Store.SetParameterData(Variable.GetData(), Variable, true);
            Target.System->PostEditChange(); Target.System->MarkPackageDirty();
        }
        return Finish(Params, Target, Before, !Proposed.IsEmpty(), Save);
    }

    TSharedPtr<FJsonObject> BindUserInput(const TSharedPtr<FJsonObject>& Params)
    {
        FTarget Target; FString Error;
        if (!Guard(Error) || !Resolve(Params, Target, Error)) return Failure(Error);
        if (!Target.Module) return Failure(TEXT("User input binding requires target=module and exact node identity"));
        auto Before = Snapshot(Target, Error);
        if (!Before || !Baseline(Params, Before, Error)) return Failure(Error);
        if (Target.Asset->GetPackage()->IsDirty()) return Failure(TEXT("Preexisting dirty asset refused"));
        FString InputName, UserName;
        if (!Params->TryGetStringField(TEXT("input_name"), InputName) || !Params->TryGetStringField(TEXT("user_parameter"), UserName) || !UserName.StartsWith(TEXT("User.")))
            return Failure(TEXT("Require canonical input_name from authoring reader and declared User.parameter"));
        FNiagaraVariable Input;
        for (const auto& Candidate : Inputs(Target)) if (Candidate.GetName().ToString() == InputName) Input = Candidate;
        if (!Input.IsValid() || Input.GetType().IsStatic() || Input.GetType().IsUObject() || Input.GetType().IsDataInterface())
            return Failure(TEXT("Input missing or unsupported static/object/data-interface type"));
        const FNiagaraVariable User(Input.GetType(), FName(*UserName));
        if (Target.System->GetExposedParameters().IndexOf(User) == INDEX_NONE) return Failure(TEXT("User variable missing or type incompatible; declare it first"));
        const auto Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input.GetName()), Target.Module);
        auto* Existing = OverridePin(Target.Module, Aliased.GetParameterHandleString());
        bool Same = false;
        bool Redirect = false;
        FString ExpectedLink;
        if (Params->HasField(TEXT("allow_simple_user_redirect")) && !Params->TryGetBoolField(TEXT("allow_simple_user_redirect"), Redirect))
            return Failure(TEXT("allow_simple_user_redirect must be bool"));
        if (Redirect)
        {
            if (!Params->TryGetStringField(TEXT("expected_linked_parameter"), ExpectedLink) || !ExpectedLink.StartsWith(TEXT("User."))
                || !Existing || Existing->LinkedTo.Num() != 1 || !Existing->LinkedTo[0]->GetOwningNode()
                || Existing->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() != TEXT("NiagaraNodeParameterMapGet")
                || Existing->LinkedTo[0]->GetOwningNode()->GetGraph() != Target.Module->GetGraph()
                || Existing->LinkedTo[0]->PinName.ToString() != ExpectedLink)
                return Failure(TEXT("Expected a single existing simple User link matching expected_linked_parameter; dynamic/expression replacements refused"));
            const FNiagaraVariable Previous(Input.GetType(), FName(*ExpectedLink));
            if (Target.System->GetExposedParameters().IndexOf(Previous) == INDEX_NONE)
                return Failure(TEXT("Old User link is missing or type incompatible; no implicit type migration"));
        }
        if (Existing && !Existing->LinkedTo.IsEmpty())
        {
            Same = Existing->LinkedTo.Num() == 1 && Existing->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet")
                && Existing->LinkedTo[0]->PinName == User.GetName();
            if (!Same && !Redirect) return Failure(TEXT("Existing linked/dynamic/expression override preserved; replacement is unsupported"));
        }
        bool DryRun = true, Save = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be bool"));
        if (DryRun)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("authoring_contract"), 1);
            Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
            Result->SetBoolField(TEXT("would_modify"), !Same); Result->SetObjectField(TEXT("before"), Before);
            Result->SetStringField(TEXT("proposed_linked_parameter"), UserName); return Result;
        }
        if (!Same)
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "NiagaraUserBinding", "Link Niagara module input to User"));
            Target.System->Modify(); Target.Handle->GetInstance().Emitter->Modify(); Target.Script->Modify(); Target.Module->GetGraph()->Modify(); Target.Module->Modify();
            auto& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Target.Module, Aliased, Input.GetType(), FGuid(), FGuid());
            Pin.GetOwningNode()->Modify();
            if (Redirect)
            {
                auto* Previous = Pin.LinkedTo[0];
                Previous->GetOwningNode()->Modify();
                Pin.BreakLinkTo(Previous);
            }
            TSet<FNiagaraVariableBase> Known; TArray<FNiagaraVariable> Users; Target.System->GetExposedParameters().GetParameters(Users);
            for (const auto& Variable : Users) Known.Add(Variable);
            FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, User, Known);
            const FString Emitter = Target.Handle->GetInstance().Emitter->GetUniqueEmitterName();
            const auto Rapid = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(FNiagaraVariable(Input.GetType(), Aliased.GetParameterHandleString()),
                *Emitter, Target.Script->GetUsage());
            Target.Script->RapidIterationParameters.RemoveParameter(Rapid);
            Target.System->PostEditChange(); Target.System->MarkPackageDirty();
        }
        auto Result = Finish(Params, Target, Before, !Same, Save);
        if (Result->GetBoolField(TEXT("success")))
        {
            auto* Pin = OverridePin(Target.Module, Aliased.GetParameterHandleString());
            if (!Pin || Pin->LinkedTo.Num() != 1 || Pin->LinkedTo[0]->PinName != User.GetName())
            { Result->SetBoolField(TEXT("success"), false); Result->SetStringField(TEXT("stage"), TEXT("readback")); Result->SetStringField(TEXT("error"), TEXT("Actual linked parameter did not match; inspect modified/saved and after")); }
        }
        return Result;
    }

    TSharedPtr<FJsonObject> RedirectUserInput(const TSharedPtr<FJsonObject>& Params)
    {
        auto Arguments = MakeShared<FJsonObject>();
        for (const auto& Pair : Params->Values) Arguments->SetField(Pair.Key, Pair.Value);
        Arguments->SetBoolField(TEXT("allow_simple_user_redirect"), true);
        return BindUserInput(Arguments);
    }

    TSharedPtr<FJsonObject> BindDynamicUserInput(const TSharedPtr<FJsonObject>& Params)
    {
        auto Arguments = MakeShared<FJsonObject>();
        for (const auto& Pair : Params->Values) Arguments->SetField(Pair.Key, Pair.Value);
        Arguments->SetStringField(TEXT("target"), TEXT("dynamic_input"));
        return BindUserInput(Arguments);
    }

    TSharedPtr<FJsonObject> MultiplyFloatInput(const TSharedPtr<FJsonObject>& Params)
    {
        FTarget Target; FString Error;
        if (!Guard(Error) || !Resolve(Params, Target, Error)) return Failure(Error);
        if (!Target.Module) return Failure(TEXT("Float wrapping requires exact module/dynamic_input target"));
        auto Before = Snapshot(Target, Error);
        if (!Before || !Baseline(Params, Before, Error)) return Failure(Error);
        if (Target.Asset->GetPackage()->IsDirty()) return Failure(TEXT("Preexisting dirty asset refused"));
        FString InputName, UserName, NodeId, PinId, VersionId;
        FGuid SourceGuid, SourcePinId, MathVersion;
        if (!Params->TryGetStringField(TEXT("input_name"), InputName) || !Params->TryGetStringField(TEXT("user_parameter"), UserName)
            || !UserName.StartsWith(TEXT("User.")) || !Params->TryGetStringField(TEXT("expected_source_node_guid"), NodeId)
            || !FGuid::Parse(NodeId, SourceGuid) || !Params->TryGetStringField(TEXT("expected_source_pin_id"), PinId) || !FGuid::Parse(PinId, SourcePinId)
            || !Params->TryGetStringField(TEXT("multiplier_version_guid"), VersionId) || !FGuid::Parse(VersionId, MathVersion) || !MathVersion.IsValid())
            return Failure(TEXT("Require canonical input, User Float, exact expected source node/pin and explicit multiplier_version_guid"));
        FNiagaraVariable Input;
        for (const auto& Candidate : Inputs(Target)) if (Candidate.GetName().ToString() == InputName) Input = Candidate;
        if (Input.GetType() != FNiagaraTypeDefinition::GetFloatDef()) return Failure(TEXT("Only non-static Float result inputs may be wrapped; vector/color/time/curve replacement is not implied"));
        FNiagaraVariable User(FNiagaraTypeDefinition::GetFloatDef(), FName(*UserName));
        if (Target.System->GetExposedParameters().IndexOf(User) == INDEX_NONE) return Failure(TEXT("Declare a type-compatible User Float before wrapping"));
        const auto Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input.GetName()), Target.Module);
        auto* Pin = OverridePin(Target.Module, Aliased.GetParameterHandleString());
        if (!Pin || Pin->LinkedTo.Num() != 1 || !Pin->LinkedTo[0]->GetOwningNode() || Pin->LinkedTo[0]->GetOwningNode()->NodeGuid != SourceGuid
            || Pin->LinkedTo[0]->PinId != SourcePinId || Pin->LinkedTo[0]->GetOwningNode()->GetGraph() != Target.Module->GetGraph())
            return Failure(TEXT("Expected existing result source node/pin mismatch; nothing replaced"));
        auto* Previous = Pin->LinkedTo[0];
        auto* SourceFunction = Cast<UNiagaraNodeFunctionCall>(Previous->GetOwningNode());
        if (!SourceFunction || SourceFunction->GetClass()->GetName() == TEXT("NiagaraNodeCustomHlsl") || !SourceFunction->FunctionScript
            || SourceFunction->FunctionScript->GetUsage() != ENiagaraScriptUsage::DynamicInput || !SourceFunction->FunctionScript->GetScriptData(SourceFunction->SelectedScriptVersion))
            return Failure(TEXT("Only an existing version-resolved dynamic Float result may be preserved/wrapped; expression/DI sources refused"));
        auto* Math = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(TEXT("/Niagara/DynamicInputs/Multiply/Multiply_Float")));
        if (!Math || !Math->GetScriptData(MathVersion)) return Failure(TEXT("Requested standard multiplier version unavailable; never substitute latest"));
        auto* MathSource = Cast<UNiagaraScriptSource>(Math->GetSource(MathVersion));
        if (!MathSource || !MathSource->NodeGraph) return Failure(TEXT("Selected multiplier graph unavailable"));
        TArray<FNiagaraVariable> MathInputs; TSet<FName> MathNames;
        for (UEdGraphNode* Node : MathSource->NodeGraph->Nodes)
        {
            if (!Node || Node->GetClass()->GetName() != TEXT("NiagaraNodeParameterMapGet")) continue;
            for (const auto* ParameterPin : Node->Pins)
            {
                if (!ParameterPin || ParameterPin->Direction != EGPD_Output || !ParameterPin->PinName.ToString().StartsWith(TEXT("Module."))) continue;
                const auto Type = UEdGraphSchema_Niagara::PinTypeToTypeDefinition(ParameterPin->PinType);
                if (Type == FNiagaraTypeDefinition::GetFloatDef() && !MathNames.Contains(ParameterPin->PinName))
                { MathNames.Add(ParameterPin->PinName); MathInputs.Add(FNiagaraVariable(Type, ParameterPin->PinName)); }
            }
        }
        TArray<FString> InputDescriptions;
        for (const auto& Variable : MathInputs) InputDescriptions.Add(Variable.GetName().ToString() + TEXT(":") + Variable.GetType().GetName());
        MathInputs.RemoveAll([](const FNiagaraVariable& Variable) { return Variable.GetType() != FNiagaraTypeDefinition::GetFloatDef(); });
        MathInputs.Sort([](const FNiagaraVariable& Left, const FNiagaraVariable& Right) { return Left.GetName().LexicalLess(Right.GetName()); });
        if (MathInputs.Num() != 2) return Failure(TEXT("Standard multiplier must expose exactly two Float inputs in selected version; actual inputs: ") + FString::Join(InputDescriptions, TEXT(", ")));
        bool Same = false;
        if (SourceFunction->FunctionScript == Math)
        {
            FTarget ExistingTarget = Target; ExistingTarget.Module = SourceFunction;
            const auto ScaleHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(MathInputs[1].GetName()), SourceFunction);
            auto* ScalePin = OverridePin(SourceFunction, ScaleHandle.GetParameterHandleString());
            const auto* ResolvedMath = Math->GetScriptData(SourceFunction->SelectedScriptVersion);
            const auto ValueHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(MathInputs[0].GetName()), SourceFunction);
            auto* ValuePin = OverridePin(SourceFunction, ValueHandle.GetParameterHandleString());
            Same = ResolvedMath && ResolvedMath->Version.VersionGuid == MathVersion && ValuePin && ValuePin->LinkedTo.Num() == 1
                && Cast<UNiagaraNodeFunctionCall>(ValuePin->LinkedTo[0]->GetOwningNode()) && ScalePin && ScalePin->LinkedTo.Num() == 1
                && ScalePin->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet") && ScalePin->LinkedTo[0]->PinName == User.GetName();
            if (!Same) return Failure(TEXT("Existing multiplier has different User/version; refuse nesting or silent replacement"));
        }
        bool DryRun = true, SaveAsset = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), SaveAsset)) return Failure(TEXT("save must be bool"));
        if (DryRun)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("authoring_contract"), 1); Result->SetBoolField(TEXT("success"), true);
            Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false); Result->SetBoolField(TEXT("would_modify"), !Same);
            Result->SetObjectField(TEXT("before"), Before); Result->SetStringField(TEXT("proposed_expression"), TEXT("preserved_result * ") + UserName);
            Result->SetStringField(TEXT("preserved_source_node_guid"), NodeId); Result->SetStringField(TEXT("preserved_source_pin_id"), PinId);
            return Result;
        }
        UNiagaraNodeFunctionCall* Wrapper = SourceFunction;
        if (!Same)
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "NiagaraPreservedMultiplier", "Multiply preserved Niagara result by User Float"));
            Target.System->Modify(); Target.Handle->GetInstance().Emitter->Modify(); Target.Script->Modify(); Target.Module->GetGraph()->Modify();
            Target.Module->Modify(); Pin->GetOwningNode()->Modify(); Previous->GetOwningNode()->Modify();
            Pin->BreakLinkTo(Previous);
            FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(*Pin, Math, Wrapper, FGuid(), TEXT("MCP_UserMultiplier"), MathVersion);
            if (!Wrapper)
            {
                Pin->MakeLinkTo(Previous);
                auto Result = Failure(TEXT("Multiplier node creation failed; original edge restored, inspect package state"));
                Result->SetBoolField(TEXT("modified"), true); Result->SetStringField(TEXT("stage"), TEXT("apply")); return Result;
            }
            UEdGraphPin* WrapperMap = nullptr;
            for (auto* InputPin : Wrapper->Pins) if (InputPin && InputPin->Direction == EGPD_Input
                && UEdGraphSchema_Niagara::PinTypeToTypeDefinition(InputPin->PinType) == FNiagaraTypeDefinition::GetParameterMapDef()) WrapperMap = InputPin;
            if (!WrapperMap || WrapperMap->LinkedTo.Num() != 1)
            {
                auto Result = Failure(TEXT("New multiplier parameter map unavailable; not compiled or saved"));
                Result->SetBoolField(TEXT("modified"), true); Result->SetStringField(TEXT("stage"), TEXT("apply")); return Result;
            }
            auto* Upstream = WrapperMap->LinkedTo[0];
            auto* DedicatedSet = NewObject<UNiagaraNode>(Wrapper->GetGraph(), Pin->GetOwningNode()->GetClass(), NAME_None, RF_Transactional);
            Wrapper->GetGraph()->AddNode(DedicatedSet, false, false); DedicatedSet->CreateNewGuid(); DedicatedSet->PostPlacedNewNode(); DedicatedSet->AllocateDefaultPins();
            UEdGraphPin* SetInput = nullptr; UEdGraphPin* SetOutput = nullptr;
            for (auto* MapPin : DedicatedSet->Pins) if (MapPin && UEdGraphSchema_Niagara::PinTypeToTypeDefinition(MapPin->PinType) == FNiagaraTypeDefinition::GetParameterMapDef())
            { if (MapPin->Direction == EGPD_Input) SetInput = MapPin; else SetOutput = MapPin; }
            if (!SetInput || !SetOutput)
            {
                auto Result = Failure(TEXT("Dedicated standard parameter-map node initialization failed; not compiled or saved"));
                Result->SetBoolField(TEXT("modified"), true); Result->SetStringField(TEXT("stage"), TEXT("apply")); return Result;
            }
            Wrapper->Modify(); Upstream->GetOwningNode()->Modify(); WrapperMap->BreakLinkTo(Upstream);
            SetInput->MakeLinkTo(Upstream); WrapperMap->MakeLinkTo(SetOutput);
            const auto ValueHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(MathInputs[0].GetName()), Wrapper);
            auto& ValuePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Wrapper, ValueHandle, Input.GetType(), FGuid(), FGuid());
            ValuePin.GetOwningNode()->Modify(); ValuePin.MakeLinkTo(Previous);
            const auto ScaleHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(MathInputs[1].GetName()), Wrapper);
            auto& ScalePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Wrapper, ScaleHandle, User.GetType(), FGuid(), FGuid());
            TSet<FNiagaraVariableBase> Known; Known.Add(User);
            FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(ScalePin, User, Known);
            Target.System->PostEditChange(); Target.System->MarkPackageDirty();
        }
        auto Result = Finish(Params, Target, Before, !Same, SaveAsset);
        Result->SetStringField(TEXT("wrapper_node_guid"), Wrapper->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Result->SetStringField(TEXT("preserved_source_node_guid"), NodeId); Result->SetStringField(TEXT("preserved_source_pin_id"), PinId);
        return Result;
    }

    TSharedPtr<FJsonObject> ReadMaterialParameters(const UNiagaraRendererProperties* Renderer)
    {
        auto Result = MakeShared<FJsonObject>();
        auto* Property = Renderer ? FindFProperty<FStructProperty>(Renderer->GetClass(), TEXT("MaterialParameters")) : nullptr;
        Result->SetNumberField(TEXT("material_parameter_contract"), 1);
        if (!Property || Property->Struct != FNiagaraRendererMaterialParameters::StaticStruct())
        {
            Result->SetBoolField(TEXT("supported"), false); Result->SetBoolField(TEXT("complete"), false);
            Result->SetStringField(TEXT("reason"), TEXT("Renderer does not expose Niagara MaterialParameters")); return Result;
        }
        Result->SetBoolField(TEXT("supported"), true);
        const auto* Parameters = Property->ContainerPtrToValuePtr<FNiagaraRendererMaterialParameters>(Renderer);
        auto Variable = [](const FNiagaraVariableBase& Value)
        {
            auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Value.GetName().ToString());
            Entry->SetStringField(TEXT("type"), Value.IsValid() ? Value.GetType().GetName() : TEXT("unbound")); return Entry;
        };
        TArray<TSharedPtr<FJsonValue>> Bindings;
        for (const auto& Binding : Parameters->AttributeBindings)
        {
            auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("MaterialParameterName"), Binding.MaterialParameterName.ToString());
            Entry->SetObjectField(TEXT("NiagaraVariable"), Variable(Binding.NiagaraVariable));
            Entry->SetObjectField(TEXT("ResolvedNiagaraVariable"), Variable(Binding.ResolvedNiagaraVariable));
            Entry->SetObjectField(TEXT("NiagaraChildVariable"), Variable(Binding.NiagaraChildVariable));
            Bindings.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("AttributeBindings"), Bindings);
        UnrealMCPNiagaraScalability::FReader Constants;
        const auto ConstantFields = Constants.Structure(FNiagaraRendererMaterialParameters::StaticStruct(), Parameters, TEXT("material"));
        for (const TCHAR* Name : {TEXT("ScalarParameters"), TEXT("VectorParameters"), TEXT("TextureParameters")}) Result->SetField(Name, ConstantFields->Values[Name]);
        TArray<TSharedPtr<FJsonValue>> Static;
        for (const auto& Parameter : Parameters->StaticBoolParameters)
        {
            auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("MaterialParameterName"), Parameter.MaterialParameterName.ToString());
            Entry->SetStringField(TEXT("StaticVariableName"), Parameter.StaticVariableName.ToString());
            if (Parameter.StaticValue.IsSet()) Entry->SetBoolField(TEXT("StaticValue"), Parameter.StaticValue.GetValue());
            else Entry->SetField(TEXT("StaticValue"), MakeShared<FJsonValueNull>());
            Static.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("StaticBoolParameters"), Static);
        TArray<TSharedPtr<FJsonValue>> Unsupported;
        for (const auto& Value : Constants.Unsupported)
        {
            const FString Path = Value->AsObject()->GetStringField(TEXT("path"));
            if (Path.StartsWith(TEXT("material.StaticBoolParameters[")) && Path.EndsWith(TEXT(".StaticValue"))) continue;
            Unsupported.Add(Value);
        }
        TArray<UMaterialInterface*> Materials; Renderer->GetUsedMaterials(nullptr, Materials);
        TArray<TSharedPtr<FJsonValue>> Sources;
        for (auto* Material : Materials)
        {
            if (!Material) { Sources.Add(MakeShared<FJsonValueNull>()); continue; }
            auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), Material->GetPathName());
            for (const FString& Kind : {FString(TEXT("scalar")), FString(TEXT("vector"))})
            {
                TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Ids;
                if (Kind == TEXT("scalar")) Material->GetAllScalarParameterInfo(Infos, Ids); else Material->GetAllVectorParameterInfo(Infos, Ids);
                TArray<TSharedPtr<FJsonValue>> Names;
                for (const auto& Info : Infos)
                {
                    auto Name = MakeShared<FJsonObject>(); Name->SetStringField(TEXT("name"), Info.Name.ToString());
                    Name->SetNumberField(TEXT("association"), static_cast<int32>(Info.Association)); Name->SetNumberField(TEXT("index"), Info.Index);
                    Names.Add(MakeShared<FJsonValueObject>(Name));
                }
                Entry->SetArrayField(Kind + TEXT("_parameters"), Names);
            }
            Sources.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("saved_materials"), Sources);
        Result->SetArrayField(TEXT("unsupported"), Unsupported); Result->SetBoolField(TEXT("complete"), Unsupported.IsEmpty());
        Result->SetStringField(TEXT("mid_semantics"), TEXT("Nonempty bindings require engine per-instance MID processing; saved base materials are not runtime component material overrides. This read is not a runtime MID or visual acceptance."));
        return Result;
    }

    TSharedPtr<FJsonObject> SetMaterialBindings(const TSharedPtr<FJsonObject>& Params)
    {
        FTarget Target; FString Error;
        if (!Guard(Error) || !Resolve(Params, Target, Error)) return Failure(Error);
        if (Target.Kind != TEXT("renderer")) return Failure(TEXT("Material bindings require exact renderer target"));
        auto Before = Snapshot(Target, Error);
        if (!Before || !Baseline(Params, Before, Error)) return Failure(Error);
        if (Target.Asset->GetPackage()->IsDirty()) return Failure(TEXT("Preexisting dirty asset refused"));
        auto* Property = FindFProperty<FStructProperty>(Target.Owner->GetClass(), TEXT("MaterialParameters"));
        if (!Property || Property->Struct != FNiagaraRendererMaterialParameters::StaticStruct()) return Failure(TEXT("Renderer MaterialParameters unsupported"));
        auto* Current = Property->ContainerPtrToValuePtr<FNiagaraRendererMaterialParameters>(Target.Owner);
        auto Proposed = *Current;
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Params->TryGetArrayField(TEXT("bindings"), Entries) || Entries->IsEmpty() || Entries->Num() > 64)
            return Failure(TEXT("bindings requires 1..64 {material_parameter,user_parameter} mapping patches"));
        TArray<UMaterialInterface*> Materials; Cast<UNiagaraRendererProperties>(Target.Owner)->GetUsedMaterials(nullptr, Materials);
        if (Materials.IsEmpty() || Materials.Contains(nullptr)) return Failure(TEXT("Saved material sources are missing; resolve them before binding"));
        TSet<FName> Names; bool Modified = false;
        for (const auto& Entry : *Entries)
        {
            FString Name, User;
            if (Entry->Type != EJson::Object || Entry->AsObject()->Values.Num() != 2
                || !Entry->AsObject()->TryGetStringField(TEXT("material_parameter"), Name)
                || !Entry->AsObject()->TryGetStringField(TEXT("user_parameter"), User) || Name.IsEmpty() || Name.Len() >= NAME_SIZE || !User.StartsWith(TEXT("User.")))
                return Failure(TEXT("Each mapping requires exact material_parameter and User.parameter strings"));
            const FName Parameter(*Name);
            if (Names.Contains(Parameter)) return Failure(TEXT("Duplicate material parameter mapping"));
            Names.Add(Parameter);
            FNiagaraVariable Variable; TArray<FNiagaraVariable> Users; Target.System->GetExposedParameters().GetParameters(Users);
            for (const auto& Candidate : Users) if (Candidate.GetName().ToString() == User) Variable = Candidate;
            if (!Variable.IsValid() || (Variable.GetType() != FNiagaraTypeDefinition::GetFloatDef() && Variable.GetType() != FNiagaraTypeDefinition::GetColorDef()))
                return Failure(TEXT("Mapping requires a declared User Float or Color"));
            for (auto* Material : Materials)
            {
                TArray<FMaterialParameterInfo> Infos, Opposite; TArray<FGuid> Ids;
                if (Variable.GetType() == FNiagaraTypeDefinition::GetFloatDef())
                { Material->GetAllScalarParameterInfo(Infos, Ids); Material->GetAllVectorParameterInfo(Opposite, Ids); }
                else { Material->GetAllVectorParameterInfo(Infos, Ids); Material->GetAllScalarParameterInfo(Opposite, Ids); }
                int32 Matches = 0;
                for (const auto& Info : Infos) if (Info.Name == Parameter)
                { ++Matches; if (Info.Association != EMaterialParameterAssociation::GlobalParameter) return Failure(TEXT("Only unambiguous global material parameters supported")); }
                for (const auto& Info : Opposite) if (Info.Name == Parameter) return Failure(TEXT("Material parameter name has conflicting types"));
                if (Matches != 1) return Failure(TEXT("Parameter missing, wrong type or ambiguous on a saved material: ") + Material->GetPathName() + TEXT(".") + Name);
            }
            for (const auto& Constant : Proposed.ScalarParameters) if (Constant.MaterialParameterName == Parameter) return Failure(TEXT("Existing scalar constant preserved; conflicting User mapping refused"));
            for (const auto& Constant : Proposed.VectorParameters) if (Constant.MaterialParameterName == Parameter) return Failure(TEXT("Existing vector constant preserved; conflicting User mapping refused"));
            FNiagaraMaterialAttributeBinding* Binding = nullptr;
            for (auto& Candidate : Proposed.AttributeBindings) if (Candidate.MaterialParameterName == Parameter)
            { if (Binding) return Failure(TEXT("Existing duplicate material binding is ambiguous")); Binding = &Candidate; }
            if (Binding && Binding->NiagaraVariable == Variable && !Binding->NiagaraChildVariable.IsValid()) continue;
            if (!Binding) Binding = &Proposed.AttributeBindings.AddDefaulted_GetRef();
            Binding->MaterialParameterName = Parameter; Binding->NiagaraVariable = Variable;
            Binding->NiagaraChildVariable = FNiagaraVariableBase(); Binding->CacheValues(Target.Handle->GetInstance().Emitter);
            Modified = true;
        }
        bool DryRun = true, Save = false;
        if (Params->HasField(TEXT("dry_run")) && !Params->TryGetBoolField(TEXT("dry_run"), DryRun)) return Failure(TEXT("dry_run must be bool"));
        if (Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save)) return Failure(TEXT("save must be bool"));
        if (DryRun)
        {
            auto Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("authoring_contract"), 1);
            Result->SetBoolField(TEXT("success"), true); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
            Result->SetBoolField(TEXT("would_modify"), Modified); Result->SetObjectField(TEXT("before"), Before);
            Result->SetArrayField(TEXT("proposed_bindings"), *Entries); return Result;
        }
        if (Modified)
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "NiagaraMaterialBindings", "Edit Niagara User material mappings"));
            Target.System->Modify(); Target.Owner->Modify(); *Current = MoveTemp(Proposed);
            Target.Owner->PostEditChange(); Target.System->PostEditChange(); Target.System->MarkPackageDirty();
        }
        return Finish(Params, Target, Before, Modified, Save);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "MaterialEditingLibrary.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraEmitterInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraScalabilityAuthoringTest, "UnrealMCP.Niagara.Authoring.Scalability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraScalabilityAuthoringTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPNiagaraAuthoring;
    const FString Path = TEXT("/Game/__Dev/NiagaraAuthoring_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/NET_Test");
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path);
    const auto Preview = CreateEffectType(Params);
    TestTrue(TEXT("Create preview succeeds"), Preview->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Preview creates nothing"), Preview->GetBoolField(TEXT("created")));
    Params->SetBoolField(TEXT("dry_run"), false); Params->SetBoolField(TEXT("save"), true);
    const auto Created = CreateEffectType(Params);
    if (!TestTrue(TEXT("EffectType actually saved"), Created->GetBoolField(TEXT("saved")))) return false;
    TestFalse(TEXT("Same path refuses overwrite"), CreateEffectType(Params)->GetBoolField(TEXT("success")));
    auto* Effect = Cast<UNiagaraEffectType>(UEditorAssetLibrary::LoadAsset(Path));
    if (!TestNotNull(TEXT("Created correct class"), Effect)) return false;
    auto Before = Read(Params);
    Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    auto Change = MakeShared<FJsonObject>(); Change->SetStringField(TEXT("field"), TEXT("CullReaction"));
    Change->SetStringField(TEXT("value"), TEXT("DeactivateImmediate"));
    Params->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)});
    Params->SetStringField(TEXT("significance_handler"), TEXT("Age"));
    Params->SetBoolField(TEXT("dry_run"), true); Params->SetBoolField(TEXT("save"), false);
    const auto Checked = WriteScalability(Params);
    TestTrue(TEXT("Typed patch preview"), Checked->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Preview baseline unchanged"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    TestFalse(TEXT("Preview leaves package clean"), Effect->GetPackage()->IsDirty());
    Params->SetBoolField(TEXT("dry_run"), false);
    const auto Applied = WriteScalability(Params);
    TestTrue(TEXT("Applied without saving"), Applied->GetBoolField(TEXT("success")) && !Applied->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("Age handler owned by EffectType"), Effect->GetSignificanceHandler() && Effect->GetSignificanceHandler()->GetOuter() == Effect);
    TestFalse(TEXT("Existing dirty draft refused"), WriteScalability(Params)->GetBoolField(TEXT("success")));
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores complete saved fields"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Effect->GetPackage()->SetDirtyFlag(false);
    Params->SetStringField(TEXT("expected_baseline"), TEXT("stale"));
    TestFalse(TEXT("Stale baseline refused"), WriteScalability(Params)->GetBoolField(TEXT("success")));
    Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    Change->SetStringField(TEXT("field"), TEXT("Platforms"));
    TestFalse(TEXT("Wrong target field refused"), WriteScalability(Params)->GetBoolField(TEXT("success")));
    Change->SetStringField(TEXT("field"), TEXT("CullReaction")); Change->SetStringField(TEXT("value"), TEXT("NotAnEnum"));
    TestFalse(TEXT("Unknown enum refused"), WriteScalability(Params)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Rejected patches leave clean package"), Effect->GetPackage()->IsDirty());
    Change->SetStringField(TEXT("value"), TEXT("DeactivateImmediate"));
    Params->SetBoolField(TEXT("save"), true);
    FailSaveForTest = true;
    const auto FailedSave = WriteScalability(Params);
    FailSaveForTest = false;
    TestTrue(TEXT("Save failure retains memory modification"), FailedSave->GetBoolField(TEXT("modified")));
    TestFalse(TEXT("Save failure never claims saved"), FailedSave->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Separate save stage"), FailedSave->GetStringField(TEXT("stage")), FString(TEXT("save")));
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    TestTrue(TEXT("Explicit approved draft save"), SaveDraft(Params)->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("Final package clean"), Effect->GetPackage()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraParameterAuthoringTest, "UnrealMCP.Niagara.Authoring.Parameters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraParameterAuthoringTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPNiagaraAuthoring;
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPNiagaraAuthoringRoot="), Root))
        Root = TEXT("/Game/__Dev/NiagaraParameters_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!Root.StartsWith(TEXT("/Game/__Dev/")) || FPackageName::DoesPackageExist(Root + TEXT("/NS_Test")))
    { AddError(TEXT("Require an unused isolated /Game/__Dev root")); return false; }
    AddInfo(TEXT("Authoring fixture root: ") + Root);
    const FString Path = Root + TEXT("/NS_Test");
    auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), Path));
    if (!TestNotNull(TEXT("Initialized isolated NS"), System)) return false;
    if (!TestTrue(TEXT("Isolated duplicate saved"), UEditorAssetLibrary::SaveLoadedAsset(System, false))) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path); Params->SetStringField(TEXT("target"), TEXT("system"));
    auto Before = Read(Params);
    if (!TestTrue(TEXT("Complete typed system baseline"), Before->GetBoolField(TEXT("success")))) return false;
    Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    auto Rate = MakeShared<FJsonObject>(); Rate->SetStringField(TEXT("name"), TEXT("User.MCP_Rate")); Rate->SetStringField(TEXT("type"), TEXT("float")); Rate->SetNumberField(TEXT("value"), 12);
    auto Tint = MakeShared<FJsonObject>(); Tint->SetStringField(TEXT("name"), TEXT("User.MCP_Tint")); Tint->SetStringField(TEXT("type"), TEXT("color"));
    Tint->SetArrayField(TEXT("value"), {MakeShared<FJsonValueNumber>(2), MakeShared<FJsonValueNumber>(0.5), MakeShared<FJsonValueNumber>(1), MakeShared<FJsonValueNumber>(0.75)});
    auto Flag = MakeShared<FJsonObject>(); Flag->SetStringField(TEXT("name"), TEXT("User.MCP_Flag")); Flag->SetStringField(TEXT("type"), TEXT("bool")); Flag->SetBoolField(TEXT("value"), true);
    auto Alternate = MakeShared<FJsonObject>(); Alternate->SetStringField(TEXT("name"), TEXT("User.MCP_AlternateRate")); Alternate->SetStringField(TEXT("type"), TEXT("float")); Alternate->SetNumberField(TEXT("value"), 12);
    Params->SetArrayField(TEXT("parameters"), {MakeShared<FJsonValueObject>(Rate), MakeShared<FJsonValueObject>(Tint), MakeShared<FJsonValueObject>(Flag), MakeShared<FJsonValueObject>(Alternate)});
    TestTrue(TEXT("User preview succeeds"), SetUserParameters(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Preview preserves baseline"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("dry_run"), false); Params->SetBoolField(TEXT("save"), true);
    const auto Applied = SetUserParameters(Params);
    if (!TestTrue(TEXT("User declarations compiled and saved"), Applied->GetBoolField(TEXT("success")) && Applied->GetBoolField(TEXT("saved")))) return false;
    TestTrue(TEXT("Real compile reports returned"), Applied->HasField(TEXT("compile_reports")));
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("save"), false);
    const auto NoOp = SetUserParameters(Params);
    TestTrue(TEXT("Same declarations are no-op"), NoOp->GetBoolField(TEXT("success")) && !NoOp->GetBoolField(TEXT("modified")));
    Rate->SetStringField(TEXT("type"), TEXT("bool")); Rate->SetBoolField(TEXT("value"), false);
    TestFalse(TEXT("Existing name wrong type rejects whole batch"), SetUserParameters(Params)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Rejected User batch leaves clean"), System->GetPackage()->IsDirty());
    Rate->SetStringField(TEXT("type"), TEXT("float")); Rate->SetNumberField(TEXT("value"), 12);
    auto& Handle = System->GetEmitterHandles()[0];
    const FString HandleId = Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens);
    const bool Enabled = Handle.GetIsEnabled();
    Params->SetStringField(TEXT("target"), TEXT("emitter")); Params->SetStringField(TEXT("emitter_handle_id"), HandleId);
    Before = Read(Params);
    if (!TestTrue(TEXT("Selected emitter baseline readable"), Before->GetBoolField(TEXT("success")))) return false;
    Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    auto Change = MakeShared<FJsonObject>(); Change->SetStringField(TEXT("field"), TEXT("Platforms"));
    auto Platforms = MakeShared<FJsonObject>(); Platforms->SetNumberField(TEXT("QualityLevelMask"), -29); Change->SetObjectField(TEXT("value"), Platforms);
    Params->SetArrayField(TEXT("changes"), {MakeShared<FJsonValueObject>(Change)}); Params->SetBoolField(TEXT("save"), true);
    if (!TestTrue(TEXT("Emitter signed mask writes"), WriteScalability(Params)->GetBoolField(TEXT("success")))) return false;
    TestEqual(TEXT("Original author enabled preserved"), System->GetEmitterHandles()[0].GetIsEnabled(), Enabled);
    TestEqual(TEXT("Exact selected mask"), System->GetEmitterHandles()[0].GetEmitterData()->Platforms.QualityLevelMask, -29);
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    Platforms->SetNumberField(TEXT("QualityLevelMask"), -1);
    TestTrue(TEXT("Restore fixture full quality for runtime checks"), WriteScalability(Params)->GetBoolField(TEXT("success")));
    FNiagaraEmitterScalabilityOverrides Rules;
    auto& Low = Rules.Overrides.AddDefaulted_GetRef(); Low.Platforms = FNiagaraPlatformSet(-29);
    Low.bOverrideSpawnCountScale = true; Low.bScaleSpawnCount = true; Low.SpawnCountScale = 0.1f;
    FNiagaraDeviceProfileStateEntry Profile; Profile.ProfileName = TEXT("MCP_Test_Profile");
    Profile.SetState(0, ENiagaraPlatformSelectionState::Enabled); Profile.SetState(1, ENiagaraPlatformSelectionState::Disabled);
    Low.Platforms.DeviceProfileStates.Add(Profile);
    FNiagaraPlatformSetCVarCondition Condition; Condition.CVarName = TEXT("MCP.Authoring.Unregistered");
    Condition.bUseMinInt = true; Condition.MinInt = 7; Low.Platforms.CVarConditions.Add(Condition);
    auto& High = Rules.Overrides.AddDefaulted_GetRef(); High.Platforms = FNiagaraPlatformSet(-4);
    High.bOverrideSpawnCountScale = false; High.bScaleSpawnCount = false; High.SpawnCountScale = 1;
    auto* RuleProperty = FVersionedNiagaraEmitterData::StaticStruct()->FindPropertyByName(TEXT("ScalabilityOverrides"));
    FString RuleError;
    auto RuleValue = Encode(RuleProperty, &Rules, RuleError);
    if (!TestTrue(TEXT("All native override row fields encoded"), RuleValue.IsValid())) return false;
    Change->SetStringField(TEXT("field"), TEXT("ScalabilityOverrides")); Change->SetField(TEXT("value"), RuleValue);
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    if (!TestTrue(TEXT("Complete ordered profile/CVar rows written"), WriteScalability(Params)->GetBoolField(TEXT("success")))) return false;
    const auto& SavedRules = System->GetEmitterHandles()[0].GetEmitterData()->ScalabilityOverrides.Overrides;
    TestEqual(TEXT("Row count retained"), SavedRules.Num(), 2);
    TestEqual(TEXT("Low raw mask"), SavedRules[0].Platforms.QualityLevelMask, -29);
    TestFalse(TEXT("Inactive High saved flag preserved"), SavedRules[1].bScaleSpawnCount);
    TestEqual(TEXT("Nonempty DeviceProfile stored"), SavedRules[0].Platforms.DeviceProfileStates[0].GetState(1), ENiagaraPlatformSelectionState::Disabled);
    TestEqual(TEXT("CVar comparison stored unevaluated"), SavedRules[0].Platforms.CVarConditions[0].MinInt, 7);
    auto* Data = System->GetEmitterHandles()[0].GetEmitterData();
    auto* Source = Cast<UNiagaraScriptSource>(Data->EmitterUpdateScriptProps.Script->GetSource(FGuid()));
    UNiagaraNodeFunctionCall* Module = nullptr;
    for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
        if (auto* Call = Cast<UNiagaraNodeFunctionCall>(Node)) if (Call->FunctionScript && Call->FunctionScript->GetName() == TEXT("SpawnRate")) Module = Call;
    if (!TestNotNull(TEXT("Actual SpawnRate call"), Module)) return false;
    Params->SetStringField(TEXT("target"), TEXT("module")); Params->SetStringField(TEXT("script_type"), TEXT("emitter_update"));
    Params->SetStringField(TEXT("module_node_guid"), Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Before = Read(Params);
    if (!TestTrue(TEXT("Exact module baseline"), Before->GetBoolField(TEXT("success")))) return false;
    Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    Params->SetStringField(TEXT("input_name"), TEXT("Module.SpawnRate")); Params->SetStringField(TEXT("user_parameter"), TEXT("User.MCP_Rate"));
    Params->SetBoolField(TEXT("dry_run"), true);
    TestTrue(TEXT("Linked preview validates"), BindUserInput(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Linked preview graph unchanged"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("dry_run"), false);
    const auto Linked = BindUserInput(Params);
    if (!TestTrue(TEXT("Linked apply compiled/saved"), Linked->GetBoolField(TEXT("success")) && Linked->GetBoolField(TEXT("saved")))) return false;
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    TestFalse(TEXT("Same linked variable no-op"), BindUserInput(Params)->GetBoolField(TEXT("modified")));
    Params->SetStringField(TEXT("user_parameter"), TEXT("User.MCP_AlternateRate"));
    TestFalse(TEXT("Ordinary writer still refuses redirect"), BindUserInput(Params)->GetBoolField(TEXT("success")));
    Params->SetStringField(TEXT("expected_linked_parameter"), TEXT("User.WrongOld"));
    TestFalse(TEXT("Explicit redirect wrong old link refused"), RedirectUserInput(Params)->GetBoolField(TEXT("success")));
    Params->SetStringField(TEXT("expected_linked_parameter"), TEXT("User.MCP_Rate"));
    const auto RedirectBefore = Read(Params);
    Params->SetBoolField(TEXT("dry_run"), true);
    TestTrue(TEXT("Explicit same-type redirect preview"), RedirectUserInput(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Redirect preview preserves graph"), Read(Params)->GetStringField(TEXT("baseline")), RedirectBefore->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("dry_run"), false); Params->SetBoolField(TEXT("save"), false);
    const auto Redirected = RedirectUserInput(Params);
    TestTrue(TEXT("Explicit redirect applies"), Redirected->GetBoolField(TEXT("success")) && Redirected->GetBoolField(TEXT("modified")));
    TestTrue(TEXT("Old User declaration retained"), System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.MCP_Rate"))) != INDEX_NONE);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Redirect Undo restores exact graph"), Read(Params)->GetStringField(TEXT("baseline")), RedirectBefore->GetStringField(TEXT("baseline")));
    System->GetPackage()->SetDirtyFlag(false);
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    Params->SetStringField(TEXT("user_parameter"), TEXT("User.MCP_Rate"));
    TestFalse(TEXT("Explicit already-desired redirect no-op"), RedirectUserInput(Params)->GetBoolField(TEXT("modified")));
    Params->SetBoolField(TEXT("save"), true);
    auto* Package = CreatePackage(*(Root + TEXT("/M_Test")));
    auto* Material = NewObject<UMaterial>(Package, TEXT("M_Test"), RF_Public | RF_Standalone | RF_Transactional);
    Material->SetShadingModel(MSM_Unlit); Material->bUsedWithNiagaraSprites = true;
    auto* Scalar = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass()));
    Scalar->ParameterName = TEXT("Brightness"); Scalar->DefaultValue = 1;
    auto* Vector = Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass()));
    Vector->ParameterName = TEXT("Tint"); Vector->DefaultValue = FLinearColor::White;
    auto* Multiply = Cast<UMaterialExpressionMultiply>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass()));
    Multiply->A.Connect(0, Vector); Multiply->B.Connect(0, Scalar); Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Multiply);
    FAssetRegistryModule::AssetCreated(Material); Material->PostEditChange(); Material->MarkPackageDirty();
    TestTrue(TEXT("Fixture material saved"), UEditorAssetLibrary::SaveLoadedAsset(Material, false));
    auto* Renderer = Cast<UNiagaraSpriteRendererProperties>(System->GetEmitterHandles()[0].GetEmitterData()->GetRenderers()[0]);
    if (!TestNotNull(TEXT("Exact Sprite slot"), Renderer)) return false;
    Renderer->Modify(); System->Modify(); Renderer->Material = Material; Renderer->PostEditChange(); System->PostEditChange(); System->MarkPackageDirty();
    TestTrue(TEXT("Fixture material assignment saved"), UEditorAssetLibrary::SaveLoadedAsset(System, false));
    Params->SetStringField(TEXT("target"), TEXT("renderer")); Params->SetNumberField(TEXT("renderer_index"), 0);
    Before = Read(Params);
    if (!TestTrue(TEXT("Renderer full MaterialParameters readable"), Before->GetObjectField(TEXT("material_parameters"))->GetBoolField(TEXT("complete")))) return false;
    Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    auto Brightness = MakeShared<FJsonObject>(); Brightness->SetStringField(TEXT("material_parameter"), TEXT("Brightness")); Brightness->SetStringField(TEXT("user_parameter"), TEXT("User.MCP_Rate"));
    auto Color = MakeShared<FJsonObject>(); Color->SetStringField(TEXT("material_parameter"), TEXT("Tint")); Color->SetStringField(TEXT("user_parameter"), TEXT("User.MCP_Tint"));
    Params->SetArrayField(TEXT("bindings"), {MakeShared<FJsonValueObject>(Brightness), MakeShared<FJsonValueObject>(Color)});
    const auto Mapped = SetMaterialBindings(Params);
    if (!TestTrue(TEXT("Typed material mapping compiled/saved"), Mapped->GetBoolField(TEXT("success")) && Mapped->GetBoolField(TEXT("saved")))) return false;
    TestEqual(TEXT("Two exact AttributeBindings"), Renderer->MaterialParameters.AttributeBindings.Num(), 2);
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    TestFalse(TEXT("Same mappings are no-op"), SetMaterialBindings(Params)->GetBoolField(TEXT("modified")));
    Color->SetStringField(TEXT("material_parameter"), TEXT("MissingParameter"));
    TestFalse(TEXT("Wrong material name refused"), SetMaterialBindings(Params)->GetBoolField(TEXT("success")));
    TestFalse(TEXT("Rejected mapping leaves package clean"), System->GetPackage()->IsDirty());
    Params->SetStringField(TEXT("target"), TEXT("system"));
    Before = Read(Params); Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    Change->SetStringField(TEXT("field"), TEXT("Platforms")); Platforms->SetNumberField(TEXT("QualityLevelMask"), 31); Change->SetObjectField(TEXT("value"), Platforms);
    FailCompileForTest = true;
    const auto CompileFailure = WriteScalability(Params);
    FailCompileForTest = false;
    TestFalse(TEXT("Injected compile rejection prevents save"), CompileFailure->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("Compile failure retains applied state"), CompileFailure->GetBoolField(TEXT("modified")));
    TestEqual(TEXT("Compile distinct from save failure"), CompileFailure->GetStringField(TEXT("stage")), FString(TEXT("compile")));
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    const auto SavedDraft = SaveDraft(Params);
    TestTrue(TEXT("Approved draft compiled and saved"), SavedDraft->GetBoolField(TEXT("compiled")) && SavedDraft->GetBoolField(TEXT("saved")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraDynamicBindingsTest, "UnrealMCP.Niagara.Authoring.DynamicBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraDynamicBindingsTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPNiagaraAuthoring;
    const FString Path = TEXT("/Game/__Dev/NiagaraDynamicBindings_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/NS_Test");
    AddInfo(TEXT("Dynamic binding fixture: ") + Path);
    auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::DuplicateAsset(TEXT("/Niagara/DefaultAssets/DefaultSystem"), Path));
    if (!TestNotNull(TEXT("Initialized isolated dynamic binding fixture"), System)) return false;
    auto& Handle = System->GetEmitterHandles()[0];
    auto* Source = Cast<UNiagaraScriptSource>(Handle.GetEmitterData()->EmitterUpdateScriptProps.Script->GetSource(FGuid()));
    UNiagaraNodeFunctionCall* Module = nullptr;
    for (UEdGraphNode* Node : Source->NodeGraph->Nodes) if (auto* Call = Cast<UNiagaraNodeFunctionCall>(Node))
        if (Call->FunctionScript && Call->FunctionScript->GetName() == TEXT("SpawnRate")) Module = Call;
    if (!TestNotNull(TEXT("Actual SpawnRate fixture module"), Module)) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path); Params->SetStringField(TEXT("target"), TEXT("module"));
    Params->SetStringField(TEXT("emitter_handle_id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("script_type"), TEXT("emitter_update")); Params->SetStringField(TEXT("module_node_guid"), Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    FTarget Target; FString Error;
    if (!TestTrue(TEXT("Exact fixture context"), Resolve(Params, Target, Error))) return false;
    const FNiagaraVariable User(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.MCP_Size"));
    System->GetExposedParameters().SetParameterValue<float>(1, User, true);
    auto* CurveScript = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(TEXT("/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve")));
    if (!TestNotNull(TEXT("Exact engine FloatFromCurve"), CurveScript)) return false;
    const auto ParentHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(TEXT("Module.SpawnRate")), Module);
    auto& ParentPin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Module, ParentHandle, User.GetType(), FGuid(), FGuid());
    UNiagaraNodeFunctionCall* CurveNode = nullptr;
    const FGuid CurveVersion = CurveScript->GetScriptData(FGuid())->Version.VersionGuid;
    FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(ParentPin, CurveScript, CurveNode, FGuid(), TEXT("MCP_PreservedCurve"), CurveVersion);
    if (!TestNotNull(TEXT("Initialized curve function"), CurveNode)) return false;
    FTarget CurveTarget = Target; CurveTarget.Module = CurveNode;
    FNiagaraVariable CurveInput;
    for (const auto& Input : Inputs(CurveTarget)) if (Input.GetType().IsDataInterface() && Input.GetType().GetClass()->IsChildOf(UNiagaraDataInterfaceCurve::StaticClass())) CurveInput = Input;
    if (!TestTrue(TEXT("Canonical curve DI input"), CurveInput.IsValid())) return false;
    const auto CurveHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(CurveInput.GetName()), CurveNode);
    auto& CurvePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*CurveNode, CurveHandle, CurveInput.GetType(), FGuid(), FGuid());
    UNiagaraDataInterface* Interface = nullptr;
    FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(CurvePin, UNiagaraDataInterfaceCurve::StaticClass(), TEXT("MCP_PreservedCurveData"), Interface);
    auto* Curve = Cast<UNiagaraDataInterfaceCurve>(Interface);
    if (!TestNotNull(TEXT("Actual curve DI"), Curve)) return false;
    Curve->Curve.AddKey(0, 12); Curve->Curve.AddKey(1, 12); Curve->UpdateLUT(); Curve->PostEditChange();
    UnrealMCPNiagaraScalability::FReader CurveReader;
    const FString CurveBefore = Json(MakeShared<FJsonValueObject>(CurveReader.Structure(FRichCurve::StaticStruct(), &Curve->Curve)));
    auto* OriginalOutput = ParentPin.LinkedTo[0];
    auto* Witness = NewObject<UNiagaraNodeFunctionCall>(Module->GetGraph());
    Module->GetGraph()->AddNode(Witness, false, false); Witness->CreateNewGuid();
    Witness->FunctionScript = CurveScript; Witness->SelectedScriptVersion = CurveVersion;
    auto* WitnessPin = Witness->CreatePin(EGPD_Input, UEdGraphSchema_Niagara::TypeDefinitionToPinType(User.GetType()), TEXT("MCP_SharedSourceWitness"));
    WitnessPin->MakeLinkTo(OriginalOutput);
    const auto Rapid = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(FNiagaraVariable(User.GetType(), ParentHandle.GetParameterHandleString()),
        *Handle.GetInstance().Emitter->GetUniqueEmitterName(), Target.Script->GetUsage());
    Target.Script->RapidIterationParameters.RemoveParameter(Rapid);
    System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(); System->MarkPackageDirty();
    if (!TestTrue(TEXT("Original curve fixture saved"), UEditorAssetLibrary::SaveLoadedAsset(System, false))) return false;
    auto ParticleCount = [&](float Multiplier)
    {
        auto* World = GEditor->GetEditorWorldContext().World(); auto* Actor = World->SpawnActor<ANiagaraActor>();
        if (!Actor) return -1;
        auto* Component = Actor->GetNiagaraComponent(); Component->SetAsset(System); Component->SetVariableFloat(User.GetName(), Multiplier);
        Component->Activate(true); Component->AdvanceSimulation(60, 1.0f / 60);
        const auto Controller = Component->GetSystemInstanceController();
        const auto* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
        const int32 Count = Instance && !Instance->GetEmitters().IsEmpty() ? Instance->GetEmitters()[0]->GetTotalSpawnedParticles() : -1;
        World->DestroyActor(Actor); return Count;
    };
    const int32 BeforeCount = ParticleCount(1);
    TestTrue(TEXT("Curve fixture actually spawns particles"), BeforeCount > 0);
    auto Before = Read(Params); Params->SetStringField(TEXT("expected_baseline"), Before->GetStringField(TEXT("baseline")));
    Params->SetStringField(TEXT("input_name"), TEXT("Module.SpawnRate")); Params->SetStringField(TEXT("user_parameter"), User.GetName().ToString());
    Params->SetStringField(TEXT("expected_source_node_guid"), CurveNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("expected_source_pin_id"), OriginalOutput->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("multiplier_version_guid"), Before->GetObjectField(TEXT("standard_float_multiplier"))->GetStringField(TEXT("version_guid")));
    TestFalse(TEXT("Direct User binding still refuses curve replacement"), BindUserInput(Params)->GetBoolField(TEXT("success")));
    const auto Preview = MultiplyFloatInput(Params);
    if (!TestTrue(TEXT("Preserved-result multiplier preview"), Preview->GetBoolField(TEXT("success")))) { AddError(Preview->GetStringField(TEXT("error"))); return false; }
    TestEqual(TEXT("Preview full graph unchanged"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("dry_run"), false); Params->SetBoolField(TEXT("save"), false);
    const auto Applied = MultiplyFloatInput(Params);
    if (!TestTrue(TEXT("Multiplier applies and compiles"), Applied->GetBoolField(TEXT("success")))) { AddError(Applied->GetStringField(TEXT("error"))); return false; }
    TestTrue(TEXT("Original curve output retained in graph"), CurveNode->GetGraph() == Module->GetGraph() && !OriginalOutput->LinkedTo.IsEmpty());
    TestTrue(TEXT("Other source consumer unchanged"), WitnessPin->LinkedTo.Num() == 1 && WitnessPin->LinkedTo[0] == OriginalOutput);
    UnrealMCPNiagaraScalability::FReader AfterCurve;
    TestEqual(TEXT("All curve keys/tangents preserved"), Json(MakeShared<FJsonValueObject>(AfterCurve.Structure(FRichCurve::StaticStruct(), &Curve->Curve))), CurveBefore);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Wrapping Undo restores exact original graph"), Read(Params)->GetStringField(TEXT("baseline")), Before->GetStringField(TEXT("baseline")));
    System->GetPackage()->SetDirtyFlag(false); Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline")));
    Params->SetBoolField(TEXT("save"), true);
    const auto Saved = MultiplyFloatInput(Params);
    if (!TestTrue(TEXT("Preserved multiplier compiled/saved"), Saved->GetBoolField(TEXT("success")) && Saved->GetBoolField(TEXT("saved")))) return false;
    auto* Wrapper = Cast<UNiagaraNodeFunctionCall>(OverridePin(Module, ParentHandle.GetParameterHandleString())->LinkedTo[0]->GetOwningNode());
    Params->SetStringField(TEXT("expected_source_node_guid"), Wrapper->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("expected_source_pin_id"), OverridePin(Module, ParentHandle.GetParameterHandleString())->LinkedTo[0]->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("expected_baseline"), Read(Params)->GetStringField(TEXT("baseline"))); Params->SetBoolField(TEXT("save"), false);
    const auto Repeat = MultiplyFloatInput(Params);
    TestTrue(TEXT("Repeated multiplier no-op without nesting"), Repeat->GetBoolField(TEXT("success")) && !Repeat->GetBoolField(TEXT("modified")));
    Params->SetStringField(TEXT("target"), TEXT("dynamic_input")); Params->SetStringField(TEXT("parent_input_name"), TEXT("Module.SpawnRate"));
    Params->SetStringField(TEXT("function_node_guid"), Wrapper->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    const auto DynamicRead = Read(Params);
    if (!TestTrue(TEXT("Exact dynamic function reachable from parent input"), DynamicRead->GetBoolField(TEXT("success")))) return false;
    bool FoundScale = false;
    for (const auto& Input : DynamicRead->GetObjectField(TEXT("module"))->GetArrayField(TEXT("inputs")))
        if (Input->AsObject()->HasField(TEXT("linked_parameter")) && Input->AsObject()->GetStringField(TEXT("linked_parameter")) == User.GetName().ToString())
        { Params->SetStringField(TEXT("input_name"), Input->AsObject()->GetStringField(TEXT("name"))); FoundScale = true; }
    TestTrue(TEXT("Dynamic canonical scalar User input read"), FoundScale);
    Params->SetStringField(TEXT("expected_baseline"), DynamicRead->GetStringField(TEXT("baseline")));
    TestFalse(TEXT("Same dynamic child binding no-op"), BindDynamicUserInput(Params)->GetBoolField(TEXT("modified")));
    Params->SetStringField(TEXT("function_node_guid"), FGuid::NewGuid().ToString());
    TestFalse(TEXT("Wrong function identity refused"), Read(Params)->GetBoolField(TEXT("success")));
    TestEqual(TEXT("Multiplier1 runtime equivalence"), ParticleCount(1), BeforeCount);
    TestEqual(TEXT("Original rate independently accounts for initial frame and integer spawn count"), BeforeCount, static_cast<int32>(FMath::FloorToInt(12.0 * 59.0 / 60.0)));
    TestEqual(TEXT("Multiplier2 runtime consumption"), ParticleCount(2), static_cast<int32>(FMath::FloorToInt(12.0 * 2.0 * 59.0 / 60.0)));
    TestEqual(TEXT("Multiplier0.5 runtime consumption"), ParticleCount(0.5f), static_cast<int32>(FMath::FloorToInt(12.0 * 0.5 * 59.0 / 60.0)));
    TestFalse(TEXT("Runtime checks leave shared fixture clean"), System->GetPackage()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraDynamicBindingsReloadTest, "UnrealMCP.Niagara.Authoring.DynamicBindingsReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraDynamicBindingsReloadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPNiagaraAuthoring;
    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPDynamicBindingsReload="), Path))
    { AddInfo(TEXT("Explicit isolated cold fixture path required; not exercised in this batch")); return true; }
    if (!Path.StartsWith(TEXT("/Game/__Dev/"))) { AddError(TEXT("Only isolated dynamic fixtures allowed")); return false; }
    auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Path));
    if (!TestNotNull(TEXT("Cold persisted dynamic fixture"), System)) return false;
    const auto& Handle = System->GetEmitterHandles()[0];
    auto* Source = Cast<UNiagaraScriptSource>(Handle.GetEmitterData()->EmitterUpdateScriptProps.Script->GetSource(FGuid()));
    UNiagaraNodeFunctionCall* Module = nullptr;
    for (UEdGraphNode* Node : Source->NodeGraph->Nodes) if (auto* Call = Cast<UNiagaraNodeFunctionCall>(Node))
        if (Call->FunctionScript && Call->FunctionScript->GetName() == TEXT("SpawnRate")) Module = Call;
    if (!TestNotNull(TEXT("Cold exact module"), Module)) return false;
    const auto ParentHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(TEXT("Module.SpawnRate")), Module);
    auto* ParentPin = OverridePin(Module, ParentHandle.GetParameterHandleString());
    if (!TestTrue(TEXT("Cold exact result link"), ParentPin && ParentPin->LinkedTo.Num() == 1)) return false;
    auto* Wrapper = Cast<UNiagaraNodeFunctionCall>(ParentPin->LinkedTo[0]->GetOwningNode());
    if (!TestNotNull(TEXT("Cold wrapper node"), Wrapper)) return false;
    TestEqual(TEXT("Pinned standard script path"), Wrapper->FunctionScript->GetPathName(), FString(TEXT("/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float")));
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Path); Params->SetStringField(TEXT("target"), TEXT("dynamic_input"));
    Params->SetStringField(TEXT("emitter_handle_id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("script_type"), TEXT("emitter_update")); Params->SetStringField(TEXT("module_node_guid"), Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("parent_input_name"), TEXT("Module.SpawnRate")); Params->SetStringField(TEXT("function_node_guid"), Wrapper->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    const auto Readback = Read(Params);
    if (!TestTrue(TEXT("Cold selected dynamic chain read"), Readback->GetBoolField(TEXT("success")))) return false;
    bool HasCurve = false, HasUser = false;
    for (const auto& Input : Readback->GetObjectField(TEXT("module"))->GetArrayField(TEXT("inputs")))
    {
        const auto Entry = Input->AsObject();
        HasCurve |= Entry->HasField(TEXT("source_script_path")) && Entry->GetStringField(TEXT("source_script_path")).Contains(TEXT("FloatFromCurve"));
        HasUser |= Entry->HasField(TEXT("linked_parameter")) && Entry->GetStringField(TEXT("linked_parameter")) == TEXT("User.MCP_Size");
    }
    TestTrue(TEXT("Cold curve input preserved"), HasCurve); TestTrue(TEXT("Cold User multiplier link preserved"), HasUser);
    TestFalse(TEXT("Cold read remains clean"), System->GetPackage()->IsDirty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNiagaraAuthoringReloadTest, "UnrealMCP.Niagara.Authoring.ReloadAndMID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNiagaraAuthoringReloadTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCPNiagaraAuthoring;
    FString Root;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPNiagaraAuthoringReload="), Root))
    { AddInfo(TEXT("Cold/MID check requires explicit MCPNiagaraAuthoringReload fixture root; not exercised in this batch")); return true; }
    if (!Root.StartsWith(TEXT("/Game/__Dev/"))) { AddError(TEXT("Only isolated authoring fixture roots accepted")); return false; }
    auto* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/NS_Test")));
    auto* Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(Root + TEXT("/M_Test")));
    if (!TestNotNull(TEXT("Cold saved NS"), System) || !TestNotNull(TEXT("Cold saved material"), Material)) return false;
    auto Params = MakeShared<FJsonObject>(); Params->SetStringField(TEXT("asset_path"), Root + TEXT("/NS_Test")); Params->SetStringField(TEXT("target"), TEXT("system"));
    const FString Before = Read(Params)->GetStringField(TEXT("baseline"));
    auto& Handle = System->GetEmitterHandles()[0];
    TestEqual(TEXT("Cold ordered override rows"), Handle.GetEmitterData()->ScalabilityOverrides.Overrides.Num(), 2);
    auto* Renderer = Cast<UNiagaraSpriteRendererProperties>(Handle.GetEmitterData()->GetRenderers()[0]);
    if (!TestNotNull(TEXT("Cold renderer"), Renderer)) return false;
    TestEqual(TEXT("Cold material mapping count"), Renderer->MaterialParameters.AttributeBindings.Num(), 2);
    TestEqual(TEXT("Cold exact material reference"), Renderer->Material.Get(), static_cast<UMaterialInterface*>(Material));
    Params->SetStringField(TEXT("target"), TEXT("module")); Params->SetStringField(TEXT("emitter_handle_id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
    Params->SetStringField(TEXT("script_type"), TEXT("emitter_update"));
    auto* Source = Cast<UNiagaraScriptSource>(Handle.GetEmitterData()->EmitterUpdateScriptProps.Script->GetSource(FGuid()));
    UNiagaraNodeFunctionCall* Module = nullptr;
    for (UEdGraphNode* Node : Source->NodeGraph->Nodes) if (auto* Call = Cast<UNiagaraNodeFunctionCall>(Node))
        if (Call->FunctionScript && Call->FunctionScript->GetName() == TEXT("SpawnRate")) Module = Call;
    if (!TestNotNull(TEXT("Cold exact SpawnRate"), Module)) return false;
    Params->SetStringField(TEXT("module_node_guid"), Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    const auto ModuleRead = Read(Params);
    bool Found = false;
    for (const auto& Input : ModuleRead->GetObjectField(TEXT("module"))->GetArrayField(TEXT("inputs")))
        if (Input->AsObject()->GetStringField(TEXT("mode")) == TEXT("Linked") && Input->AsObject()->GetStringField(TEXT("linked_parameter")) == TEXT("User.MCP_Rate")) Found = true;
    TestTrue(TEXT("Linked User survives fresh load"), Found);
    auto* World = GEditor->GetEditorWorldContext().World();
    if (!TestNotNull(TEXT("Isolated commandlet Entry world"), World)) return false;
    auto* First = World->SpawnActor<ANiagaraActor>(); auto* Second = World->SpawnActor<ANiagaraActor>();
    if (!First || !Second) { AddError(TEXT("Could not spawn isolated test actors")); return false; }
    ON_SCOPE_EXIT { World->DestroyActor(First); World->DestroyActor(Second); };
    auto* FirstComponent = First->GetNiagaraComponent(); auto* SecondComponent = Second->GetNiagaraComponent();
    FirstComponent->SetAsset(System); SecondComponent->SetAsset(System);
    FirstComponent->SetVariableFloat(TEXT("User.MCP_Rate"), 2); SecondComponent->SetVariableFloat(TEXT("User.MCP_Rate"), 5);
    FirstComponent->SetVariableLinearColor(TEXT("User.MCP_Tint"), FLinearColor::Red); SecondComponent->SetVariableLinearColor(TEXT("User.MCP_Tint"), FLinearColor::Green);
    FirstComponent->Activate(true); SecondComponent->Activate(true);
    FirstComponent->AdvanceSimulation(120, 1.0f / 60); SecondComponent->AdvanceSimulation(120, 1.0f / 60);
    FirstComponent->MarkRenderDynamicDataDirty(); SecondComponent->MarkRenderDynamicDataDirty();
    World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
    const auto FirstController = FirstComponent->GetSystemInstanceController(); const auto SecondController = SecondComponent->GetSystemInstanceController();
    if (!TestTrue(TEXT("Two independent system controllers"), FirstController.IsValid() && SecondController.IsValid())) return false;
    TArray<UMaterialInterface*> FirstMaterials, SecondMaterials;
    FirstComponent->GetUsedMaterials(FirstMaterials, false); SecondComponent->GetUsedMaterials(SecondMaterials, false);
    UMaterialInstanceDynamic* FirstMID = nullptr;
    UMaterialInstanceDynamic* SecondMID = nullptr;
    for (auto* Value : FirstMaterials) if (auto* Instance = Cast<UMaterialInstanceDynamic>(Value)) if (Instance->Parent == Material) FirstMID = Instance;
    for (auto* Value : SecondMaterials) if (auto* Instance = Cast<UMaterialInstanceDynamic>(Value)) if (Instance->Parent == Material) SecondMID = Instance;
    if (!TestNotNull(TEXT("Engine-created first instance MID"), FirstMID) || !TestNotNull(TEXT("Engine-created second instance MID"), SecondMID)) return false;
    TestTrue(TEXT("MIDs are not shared"), FirstMID != SecondMID);
    float FirstValue = 0, SecondValue = 0;
    FirstMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Brightness")), FirstValue);
    SecondMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Brightness")), SecondValue);
    TestEqual(TEXT("First User brightness consumed by MID"), FirstValue, 2.0f);
    TestEqual(TEXT("Second User brightness consumed by MID"), SecondValue, 5.0f);
    FLinearColor FirstColor, SecondColor;
    FirstMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), FirstColor);
    SecondMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), SecondColor);
    TestEqual(TEXT("First User color consumed by MID"), FirstColor, FLinearColor::Red);
    TestEqual(TEXT("Second User color consumed by MID"), SecondColor, FLinearColor::Green);
    Params->SetStringField(TEXT("target"), TEXT("system"));
    TestEqual(TEXT("Instance parameters do not mutate shared NS defaults"), Read(Params)->GetStringField(TEXT("baseline")), Before);
    TestFalse(TEXT("Shared NS remains clean"), System->GetPackage()->IsDirty());
    TestFalse(TEXT("Shared base material remains clean"), Material->GetPackage()->IsDirty());
    return !HasAnyErrors();
}
#endif