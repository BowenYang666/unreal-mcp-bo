#include "Commands/UnrealMCPSceneAssets.h"
#include "Commands/UnrealMCPSceneCommands.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformFile.h"
#endif
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

namespace UnrealMCPSceneAssets
{
namespace
{
    bool Busy = false;
    constexpr int32 MaxCachedReceipts = 32;
    TMap<FString, TSharedPtr<FJsonObject>> Receipts;
    struct FDurableReceipt
    {
        FString Id;
        FMD5Hash FileHash;
    };
    TArray<FDurableReceipt> CompletedReceipts;
#if WITH_DEV_AUTOMATION_TESTS
    int32 ImportDispatchCount = 0;
#endif

    TSharedPtr<FJsonObject> Fail(const FString& Error)
    {
        auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), false);
        Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("error"), Error); Result->SetStringField(TEXT("stage"), TEXT("preflight")); return Result;
    }

    bool ProjectMatches(const TSharedPtr<FJsonObject>& Params)
    {
        FString Path;
        return Params->TryGetStringField(TEXT("project_path"), Path) && !FPaths::IsRelative(Path)
            && FPaths::IsSamePath(Path, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    }

    bool RequestId(const TSharedPtr<FJsonObject>& Params, FString& Id)
    {
        if (!Params->TryGetStringField(TEXT("request_id"), Id) || Id.IsEmpty() || Id.Len() > 64) return false;
        for (TCHAR Character : Id) if (!(FChar::IsAlnum(Character) || Character == TEXT('_'))) return false;
        return true;
    }

    FString ReceiptFile(const FString& Id) { return FPaths::ProjectSavedDir() / TEXT("UnrealMCP/SceneImports") / (Id + TEXT(".json")); }
    bool Persist(const FString& Id, const TSharedPtr<FJsonObject>& Receipt)
    {
        const FString Filename = FPaths::ConvertRelativePathToFull(ReceiptFile(Id));
        const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        auto Snapshot = MakeShared<FJsonObject>(*Receipt);
        Snapshot->SetBoolField(TEXT("receipt_persisted"), true);
        FString Json;
        bool Written = FJsonSerializer::Serialize(Snapshot, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (Written)
        {
#if PLATFORM_WINDOWS
            Written = ::MoveFileExW(*Temporary, *Filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            Written = ::rename(TCHAR_TO_UTF8(*Temporary), TCHAR_TO_UTF8(*Filename)) == 0;
#endif
        }
        if (!Written) IFileManager::Get().Delete(*Temporary, false, false, true);
        Receipt->SetBoolField(TEXT("receipt_persisted"), Written);
        return Written;
    }

    bool IsTerminal(const TSharedPtr<FJsonObject>& Receipt)
    {
        FString State;
        return Receipt->TryGetStringField(TEXT("state"), State) && (State == TEXT("completed") || State == TEXT("failed"));
    }

    void PersistTerminal(const FString& Id, const TSharedPtr<FJsonObject>& Receipt)
    {
        CompletedReceipts.RemoveAll([&](const FDurableReceipt& Entry) { return Entry.Id == Id; });
        Receipt->RemoveField(TEXT("persistence_error"));
        if (Persist(Id, Receipt) && IsTerminal(Receipt))
        {
            const FMD5Hash Hash = FMD5Hash::HashFile(*ReceiptFile(Id));
            if (Hash.IsValid()) CompletedReceipts.Add({Id, Hash});
        }
        else
        {
            Receipt->SetStringField(TEXT("persistence_error"), TEXT("Final receipt was not persisted; retained in memory, import will not be repeated"));
        }
    }

    bool MakeReceiptRoom()
    {
        while (Receipts.Num() >= MaxCachedReceipts && !CompletedReceipts.IsEmpty())
        {
            const FDurableReceipt Candidate = CompletedReceipts[0];
            CompletedReceipts.RemoveAt(0);
            const auto Found = Receipts.Find(Candidate.Id);
            if (!Found || !IsTerminal(*Found)) continue;
            const FMD5Hash CurrentHash = FMD5Hash::HashFile(*ReceiptFile(Candidate.Id));
            if (!CurrentHash.IsValid() || CurrentHash != Candidate.FileHash) continue;
            Receipts.Remove(Candidate.Id);
        }
        return Receipts.Num() < MaxCachedReceipts;
    }

    TSharedPtr<FJsonObject> AdmissionFailure(const FString& Code, const FString& Message)
    {
        auto Result = Fail(Message);
        Result->SetStringField(TEXT("error_code"), Code);
        Result->SetNumberField(TEXT("cached_receipts"), Receipts.Num());
        Result->SetNumberField(TEXT("cache_limit"), MaxCachedReceipts);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y), MakeShared<FJsonValueNumber>(Value.Z)};
    }
}

bool IsBusy() { return Busy; }

#if WITH_DEV_AUTOMATION_TESTS
int32 CachedReceiptCountForTests() { return Receipts.Num(); }
bool HasCachedReceiptForTests(const FString& Id) { return Receipts.Contains(Id); }
int32 ImportDispatchCountForTests() { return ImportDispatchCount; }
#endif

TSharedPtr<FJsonObject> Inspect(const TSharedPtr<FJsonObject>& Params)
{
    if (!ProjectMatches(Params)) return Fail(TEXT("Project mismatch"));
    FString Path;
    if (!Params->TryGetStringField(TEXT("asset_path"), Path) || !Path.StartsWith(TEXT("/"))) return Fail(TEXT("Full asset_path required"));
    UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
    if (!Asset) return Fail(TEXT("Asset not found"));
    auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Asset->GetPathName()); Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
    Result->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty());
    if (auto Mesh = Cast<UStaticMesh>(Asset))
    {
        Result->SetBoolField(TEXT("compiling"), Mesh->IsCompiling());
        if (Mesh->IsCompiling()) return Result;
        Result->SetArrayField(TEXT("bounds_origin_cm"), VectorJson(Mesh->GetBounds().Origin));
        Result->SetArrayField(TEXT("bounds_extent_cm"), VectorJson(Mesh->GetBounds().BoxExtent));
        Result->SetBoolField(TEXT("nanite_enabled"), Mesh->GetNaniteSettings().bEnabled);
        TArray<TSharedPtr<FJsonValue>> Slots, Lods;
        for (int32 Index = 0; Index < Mesh->GetStaticMaterials().Num(); ++Index)
        {
            const auto& Material = Mesh->GetStaticMaterials()[Index]; auto Slot = MakeShared<FJsonObject>();
            Slot->SetNumberField(TEXT("index"), Index); Slot->SetStringField(TEXT("name"), Material.MaterialSlotName.ToString());
            Slot->SetStringField(TEXT("material"), Material.MaterialInterface ? Material.MaterialInterface->GetPathName() : FString()); Slots.Add(MakeShared<FJsonValueObject>(Slot));
        }
        if (auto Data = Mesh->GetRenderData()) for (int32 Index = 0; Index < Data->LODResources.Num(); ++Index)
        {
            const auto& Lod = Data->LODResources[Index]; auto Entry = MakeShared<FJsonObject>();
            Entry->SetNumberField(TEXT("lod"), Index); Entry->SetNumberField(TEXT("vertices"), Lod.GetNumVertices());
            Entry->SetNumberField(TEXT("triangles"), Lod.GetNumTriangles()); Entry->SetNumberField(TEXT("uv_channels"), Lod.GetNumTexCoords());
            Entry->SetNumberField(TEXT("sections"), Lod.Sections.Num()); Lods.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("material_slots"), Slots); Result->SetArrayField(TEXT("lods"), Lods);
        auto Body = Mesh->GetBodySetup(); Result->SetNumberField(TEXT("simple_collision_elements"), Body ? Body->AggGeom.GetElementCount() : 0);
        Result->SetStringField(TEXT("collision_complexity"), Body ? StaticEnum<ECollisionTraceFlag>()->GetNameStringByValue(Body->CollisionTraceFlag) : TEXT("none"));
    }
    else if (auto Texture = Cast<UTexture2D>(Asset))
    {
        Result->SetNumberField(TEXT("width"), Texture->GetSizeX()); Result->SetNumberField(TEXT("height"), Texture->GetSizeY());
        Result->SetBoolField(TEXT("srgb"), Texture->SRGB);
        Result->SetStringField(TEXT("compression"), StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(Texture->CompressionSettings));
    }
    else return Fail(TEXT("Only StaticMesh and Texture2D assets are supported by this domain reader"));
    return Result;
}

TSharedPtr<FJsonObject> Status(const TSharedPtr<FJsonObject>& Params)
{
    FString Id;
    if (!ProjectMatches(Params) || !RequestId(Params, Id)) return Fail(TEXT("Project mismatch or invalid request_id"));
    if (const auto Found = Receipts.Find(Id)) return *Found;
    FString Json; TSharedPtr<FJsonObject> Receipt;
    FString StoredId, State;
    if (!FFileHelper::LoadFileToString(Json, *ReceiptFile(Id)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Receipt)
        || !Receipt || !Receipt->TryGetStringField(TEXT("request_id"), StoredId) || StoredId != Id || !Receipt->TryGetStringField(TEXT("state"), State))
    {
        auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("request_id"), Id); Result->SetStringField(TEXT("stage"), TEXT("receipt_load"));
        Result->SetStringField(TEXT("error_code"), TEXT("receipt_unavailable")); Result->SetStringField(TEXT("state"), TEXT("interrupted_unknown"));
        Result->SetStringField(TEXT("error"), TEXT("Import receipt missing, unreadable or invalid; inspect destination before any retry"));
        return Result;
    }
    if (!IsTerminal(Receipt))
    {
        Receipt->SetBoolField(TEXT("success"), false); Receipt->SetStringField(TEXT("state"), TEXT("interrupted_unknown"));
        Receipt->SetStringField(TEXT("error"), TEXT("Editor session ended before final receipt; inspect destination, never blindly reimport"));
    }
    return Receipt;
}

TSharedPtr<FJsonObject> Import(const TSharedPtr<FJsonObject>& Params)
{
    FString Id, Source, Destination;
    if (!ProjectMatches(Params) || !RequestId(Params, Id)) return Fail(TEXT("Project mismatch or invalid request_id"));
    if (Receipts.Contains(Id) || IFileManager::Get().FileExists(*ReceiptFile(Id))) return Status(Params);
    if (Busy || UnrealMCPScene::IsBusy()) return AdmissionFailure(TEXT("scene_task_busy"), TEXT("Scene task active; query its status before starting another import"));
    if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor) return Fail(TEXT("Import requires editor outside PIE/Simulate"));
    if (!Params->TryGetStringField(TEXT("source_file"), Source) || FPaths::IsRelative(Source) || !IFileManager::Get().FileExists(*Source)) return Fail(TEXT("source_file must be an existing absolute file"));
    const FString Extension = FPaths::GetExtension(Source).ToLower();
    if (Extension != TEXT("fbx") && Extension != TEXT("gltf") && Extension != TEXT("glb") && Extension != TEXT("png")) return Fail(TEXT("Supported import extensions: fbx, gltf, glb, png"));
    if (!Params->TryGetStringField(TEXT("destination_path"), Destination) || !Destination.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Destination) || Destination.EndsWith(TEXT("/"))) return Fail(TEXT("destination_path must be a NEW /Game directory without trailing slash"));
    const FString Directory = FPackageName::LongPackageNameToFilename(Destination);
    if (UEditorAssetLibrary::DoesDirectoryExist(Destination) || IFileManager::Get().DirectoryExists(*Directory) || UEditorAssetLibrary::DoesAssetExist(Destination)) return Fail(TEXT("Destination exists; overwrites/reimports forbidden"));
    bool Save = false, Srgb = true; double Scale = 1; int32 Timeout = 120; FString Compression = TEXT("default");
    if ((Params->HasField(TEXT("save")) && !Params->TryGetBoolField(TEXT("save"), Save))
        || (Params->HasField(TEXT("srgb")) && !Params->TryGetBoolField(TEXT("srgb"), Srgb))
        || (Params->HasField(TEXT("import_uniform_scale")) && !Params->TryGetNumberField(TEXT("import_uniform_scale"), Scale))
        || !FMath::IsFinite(Scale) || Scale <= 0 || Scale > 10000
        || (Params->HasField(TEXT("timeout_seconds")) && !Params->TryGetNumberField(TEXT("timeout_seconds"), Timeout)) || Timeout < 5 || Timeout > 600
        || (Params->HasField(TEXT("compression")) && !Params->TryGetStringField(TEXT("compression"), Compression))) return Fail(TEXT("Invalid import settings"));
    if (Compression != TEXT("default") && Compression != TEXT("normalmap") && Compression != TEXT("masks")) return Fail(TEXT("compression must be default, normalmap or masks"));
    if (Extension != TEXT("png") && (Params->HasField(TEXT("srgb")) || Params->HasField(TEXT("compression")))) return Fail(TEXT("Texture settings require PNG input"));
    if (Extension != TEXT("fbx") && Scale != 1) return Fail(TEXT("Explicit uniform scale currently supported only for FBX; glTF uses its standard units/axis conversion"));
    if (Extension == TEXT("png") && Compression != TEXT("default") && Srgb) return Fail(TEXT("Normal/mask textures require srgb=false"));
    if (!MakeReceiptRoom()) return AdmissionFailure(TEXT("receipt_cache_full"), TEXT("Receipt cache full with no safely evictable records; unresolved or non-durable receipts are retained"));
    auto Task = NewObject<UAssetImportTask>(); Task->AddToRoot();
    Task->Filename = Source; Task->DestinationPath = Destination; Task->bAutomated = true; Task->bAsync = true;
    Task->bReplaceExisting = false; Task->bReplaceExistingSettings = false; Task->bSave = false;
    if (Extension == TEXT("fbx"))
    {
        auto Factory = NewObject<UFbxFactory>(Task); Task->Factory = Factory;
        auto Options = NewObject<UFbxImportUI>(Task); Options->bImportAsSkeletal = false; Options->bImportMesh = true;
        Options->bAutomatedImportShouldDetectType = false;
        Options->MeshTypeToImport = FBXIT_StaticMesh; Options->bImportAnimations = false; Options->bImportMaterials = false; Options->bImportTextures = false;
        Options->StaticMeshImportData->ImportUniformScale = Scale; Options->StaticMeshImportData->bCombineMeshes = false;
        Options->StaticMeshImportData->bAutoGenerateCollision = true; Options->StaticMeshImportData->bGenerateLightmapUVs = true;
        Options->StaticMeshImportData->bConvertScene = true; Options->StaticMeshImportData->bConvertSceneUnit = true;
        Task->Options = Options;
    }
    auto Receipt = MakeShared<FJsonObject>(); Receipt->SetBoolField(TEXT("success"), true); Receipt->SetStringField(TEXT("state"), TEXT("pending"));
    Receipt->SetStringField(TEXT("request_id"), Id); Receipt->SetStringField(TEXT("source_file"), Source); Receipt->SetStringField(TEXT("destination_path"), Destination);
    Receipt->SetStringField(TEXT("source_md5"), LexToString(FMD5Hash::HashFile(*Source))); Receipt->SetBoolField(TEXT("saved"), false);
    Receipt->SetStringField(TEXT("pipeline"), Extension == TEXT("fbx") ? TEXT("FBX static-mesh factory") : TEXT("engine registered importer / Interchange"));
    Receipt->SetStringField(TEXT("conversion"), Extension == TEXT("gltf") || Extension == TEXT("glb") ? TEXT("glTF standard meters/Y-up to UE cm/Z-up; inspect bounds") : TEXT("source metadata to UE cm/Z-up; inspect bounds"));
    Receipt->SetNumberField(TEXT("import_uniform_scale"), Scale); Receipts.Add(Id, Receipt);
    if (!Persist(Id, Receipt)) { Receipts.Remove(Id); Task->RemoveFromRoot(); return Fail(TEXT("Cannot persist import intent; no import started")); }
    Busy = true; const double Started = FPlatformTime::Seconds();
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Task, Receipt, Id, Destination, Save, Srgb, Compression, Extension, Timeout, Started, Dispatched = false](float DeltaTime) mutable
    {
        if (!Dispatched)
        {
            Dispatched = true; Receipt->SetStringField(TEXT("state"), TEXT("running")); Persist(Id, Receipt);
#if WITH_DEV_AUTOMATION_TESTS
            ++ImportDispatchCount;
#endif
            FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Task});
        }
        if (!Task->IsAsyncImportComplete())
        {
            if (FPlatformTime::Seconds() - Started > Timeout && Receipt->GetStringField(TEXT("state")) != TEXT("overdue"))
            {
                Receipt->SetStringField(TEXT("state"), TEXT("overdue")); Receipt->SetStringField(TEXT("message"), TEXT("Import still running; no automatic cancellation/retry; keep querying status")); Persist(Id, Receipt);
            }
            return true;
        }
        bool Success = !Task->GetObjects().IsEmpty(); bool AllSaved = Save && Success;
        TArray<TSharedPtr<FJsonValue>> Assets;
        for (UObject* Asset : Task->GetObjects())
        {
            auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("asset_path"), Asset->GetPathName());
            Entry->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
            const bool Owned = Asset->GetPackage()->GetName().StartsWith(Destination + TEXT("/"));
            if (Owned && Extension == TEXT("png")) if (auto Texture = Cast<UTexture2D>(Asset))
            {
                Texture->Modify(); Texture->SRGB = Srgb;
                Texture->CompressionSettings = Compression == TEXT("normalmap") ? TC_Normalmap : Compression == TEXT("masks") ? TC_Masks : TC_Default;
                Texture->PostEditChange(); Texture->MarkPackageDirty();
            }
            const bool Saved = Owned && Save && UEditorAssetLibrary::SaveLoadedAsset(Asset, false) && !Asset->GetPackage()->IsDirty();
            Success &= Owned && (!Save || Saved); AllSaved &= Saved;
            Entry->SetBoolField(TEXT("saved"), Saved); Entry->SetBoolField(TEXT("package_dirty"), Asset->GetPackage()->IsDirty()); Assets.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Receipt->SetArrayField(TEXT("assets"), Assets); Receipt->SetBoolField(TEXT("success"), Success);
        Receipt->SetBoolField(TEXT("saved"), AllSaved); Receipt->SetBoolField(TEXT("modified"), !Assets.IsEmpty());
        Receipt->SetStringField(TEXT("state"), Success ? TEXT("completed") : TEXT("failed"));
        if (!Success) Receipt->SetStringField(TEXT("error"), TEXT("Import/save incomplete or output outside destination; inspect returned assets and editor logs; no rollback promised"));
        PersistTerminal(Id, Receipt);
        Task->RemoveFromRoot(); Busy = false; return false;
    }));
    return Receipt;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCPImportReceiptColdReadTest, "UnrealMCP.Scene.ImportReceiptColdRead", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FMCPImportReceiptColdReadTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    FString Id;
    if (FParse::Value(FCommandLine::Get(), TEXT("MCPReceiptReplay="), Id))
    {
        Names.Add(TEXT("SavedTerminal")); Commands.Add(Id);
    }
}

bool FMCPImportReceiptColdReadTest::RunTest(const FString& Id)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("UnrealMCPNoServer")) || !Id.StartsWith(TEXT("ReceiptCapacity_"))
        || !Receipts.IsEmpty() || Busy || ImportDispatchCount != 0)
    {
        AddError(TEXT("Cold replay requires a fresh isolated editor and a receipt capacity test ID")); return false;
    }
    auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("request_id"), Id);
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    const FMD5Hash Before = FMD5Hash::HashFile(*ReceiptFile(Id));
    const auto Read = Status(Request);
    if (!TestTrue(TEXT("Read completed receipt after restart"), Read->GetBoolField(TEXT("success")))) return false;
    TestEqual(TEXT("Completed state preserved"), Read->GetStringField(TEXT("state")), FString(TEXT("completed")));
    TestTrue(TEXT("Asset save outcome preserved"), Read->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("Persistence marker on disk"), Read->GetBoolField(TEXT("receipt_persisted")));
    const auto Replay = Import(Request);
    TestEqual(TEXT("Replay returns same ID"), Replay->GetStringField(TEXT("request_id")), Id);
    TestEqual(TEXT("Replay returns original asset list"), Replay->GetArrayField(TEXT("assets")).Num(), Read->GetArrayField(TEXT("assets")).Num());
    TestEqual(TEXT("No importer calls after restart"), ImportDispatchCount, 0);
    TestFalse(TEXT("Replay did not start a task"), Busy);
    TestTrue(TEXT("Disk reads leave cache empty"), Receipts.IsEmpty());
    TestTrue(TEXT("Replay does not rewrite journal"), Before.IsValid() && Before == FMD5Hash::HashFile(*ReceiptFile(Id)));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPImportReceiptSafetyTest, "UnrealMCP.Scene.ImportReceiptSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPImportReceiptSafetyTest::RunTest(const FString& Parameters)
{
    if (Busy || UnrealMCPScene::IsBusy() || !FParse::Param(FCommandLine::Get(), TEXT("UnrealMCPNoServer")))
    {
        AddError(TEXT("Receipt safety tests require an idle isolated -UnrealMCPNoServer process")); return false;
    }
    auto PreviousReceipts = MoveTemp(Receipts);
    auto PreviousCompleted = MoveTemp(CompletedReceipts);
    TArray<FString> Files;
    ON_SCOPE_EXIT
    {
        Receipts = MoveTemp(PreviousReceipts); CompletedReceipts = MoveTemp(PreviousCompleted); Busy = false;
        for (const FString& File : Files)
        {
            FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
            IFileManager::Get().Delete(*File, false, false, true);
        }
    };
    const FString Prefix = TEXT("ReceiptSafety_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    auto Seed = [&](const FString& Suffix, const FString& State)
    {
        const FString Id = Prefix + Suffix;
        auto Receipt = MakeShared<FJsonObject>();
        Receipt->SetStringField(TEXT("request_id"), Id); Receipt->SetStringField(TEXT("state"), State);
        Receipt->SetBoolField(TEXT("success"), State == TEXT("completed")); Receipt->SetBoolField(TEXT("saved"), false);
        Receipt->SetBoolField(TEXT("modified"), true); Receipt->SetArrayField(TEXT("assets"), {});
        Receipts.Add(Id, Receipt); Files.Add(ReceiptFile(Id)); return Receipt;
    };
    for (const FString State : {TEXT("pending"), TEXT("running"), TEXT("overdue"), TEXT("interrupted_unknown")})
    {
        auto Receipt = Seed(TEXT("_") + State, State);
        TestTrue(TEXT("Nonterminal record persists"), Persist(Receipt->GetStringField(TEXT("request_id")), Receipt));
    }
    auto Failed = Seed(TEXT("_failed"), TEXT("failed"));
    Failed->SetStringField(TEXT("error"), TEXT("Partial import failure"));
    auto Asset = MakeShared<FJsonObject>(); Asset->SetStringField(TEXT("asset_path"), TEXT("/Game/__Dev/SceneTools/Partial")); Asset->SetBoolField(TEXT("saved"), false);
    Failed->SetArrayField(TEXT("assets"), {MakeShared<FJsonValueObject>(Asset)});
    PersistTerminal(Failed->GetStringField(TEXT("request_id")), Failed);
    auto Preview = Seed(TEXT("_preview"), TEXT("completed"));
    PersistTerminal(Preview->GetStringField(TEXT("request_id")), Preview);
    for (int32 Index = Receipts.Num(); Index < MaxCachedReceipts; ++Index) Seed(FString::Printf(TEXT("_pinned_%d"), Index), TEXT("failed"));
    TestTrue(TEXT("Eviction makes room at capacity"), MakeReceiptRoom());
    TestFalse(TEXT("Oldest durable terminal evicted first"), Receipts.Contains(Failed->GetStringField(TEXT("request_id"))));
    TestTrue(TEXT("Newer preview receipt still cached"), Receipts.Contains(Preview->GetStringField(TEXT("request_id"))));
    Seed(TEXT("_replacement"), TEXT("pending"));
    TestTrue(TEXT("Unsaved assets do not pin durable terminal receipts"), MakeReceiptRoom());
    TestFalse(TEXT("Preview receipt evicted without saving assets"), Receipts.Contains(Preview->GetStringField(TEXT("request_id"))));
    Request->SetStringField(TEXT("request_id"), Preview->GetStringField(TEXT("request_id")));
    TestFalse(TEXT("Disk preview still reports saved false"), Status(Request)->GetBoolField(TEXT("saved")));
    Request->SetStringField(TEXT("request_id"), Failed->GetStringField(TEXT("request_id")));
    const int32 Dispatches = ImportDispatchCount;
    const auto ReplayedFailure = Import(Request);
    TestEqual(TEXT("Failed terminal replay preserves error"), ReplayedFailure->GetStringField(TEXT("error")), FString(TEXT("Partial import failure")));
    TestTrue(TEXT("Partial modification state preserved"), ReplayedFailure->GetBoolField(TEXT("modified")));
    TestEqual(TEXT("Partial asset list preserved"), ReplayedFailure->GetArrayField(TEXT("assets")).Num(), 1);
    TestEqual(TEXT("Failure replay never invokes importer"), ImportDispatchCount, Dispatches);
    Seed(TEXT("_last_pinned"), TEXT("completed"));
    TestFalse(TEXT("Cannot evict pending/running/overdue/unknown or non-durable terminal records"), MakeReceiptRoom());
    TestEqual(TEXT("Protected cache intact"), Receipts.Num(), MaxCachedReceipts);
    for (const FString State : {TEXT("pending"), TEXT("running"), TEXT("overdue"), TEXT("interrupted_unknown")})
        TestTrue(TEXT("Protected state retained: ") + State, Receipts.Contains(Prefix + TEXT("_") + State));

    const FString Source = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SceneTools") / (Prefix + TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Source), true); Files.Add(Source);
    TestTrue(TEXT("Write preflight-only source"), FFileHelper::SaveStringToFile(TEXT("capacity must reject before importer"), *Source));
    Request->SetStringField(TEXT("request_id"), Prefix + TEXT("_new")); Request->SetStringField(TEXT("source_file"), Source);
    Request->SetStringField(TEXT("destination_path"), TEXT("/Game/__Dev/SceneTools/") + Prefix);
    auto Full = Import(Request);
    TestEqual(TEXT("Capacity has distinct code"), Full->GetStringField(TEXT("error_code")), FString(TEXT("receipt_cache_full")));
    TestEqual(TEXT("Capacity receipt includes limit"), Full->GetIntegerField(TEXT("cache_limit")), MaxCachedReceipts);
    Busy = true;
    TestEqual(TEXT("Busy has distinct code"), Import(Request)->GetStringField(TEXT("error_code")), FString(TEXT("scene_task_busy")));
    Request->SetStringField(TEXT("request_id"), Failed->GetStringField(TEXT("request_id")));
    TestEqual(TEXT("Known ID replays even while busy/full"), Import(Request)->GetStringField(TEXT("state")), FString(TEXT("failed")));
    Busy = false;
    TestEqual(TEXT("Rejected requests never invoke importer"), ImportDispatchCount, Dispatches);

    Receipts.Reset(); CompletedReceipts.Reset();
    auto Changing = Seed(TEXT("_publish_failure"), TEXT("pending"));
    const FString ChangingId = Changing->GetStringField(TEXT("request_id"));
    const FString Filename = ReceiptFile(ChangingId);
    TestTrue(TEXT("Initial intent stored"), Persist(ChangingId, Changing));
    const FMD5Hash Before = FMD5Hash::HashFile(*Filename);
#if PLATFORM_WINDOWS
    if (TestTrue(TEXT("Protect old receipt to provoke replacement failure"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, true)))
    {
        Changing->SetStringField(TEXT("state"), TEXT("completed")); Changing->SetBoolField(TEXT("success"), true); Changing->SetBoolField(TEXT("saved"), true);
        PersistTerminal(ChangingId, Changing);
        TestFalse(TEXT("Failed publish is reported"), Changing->GetBoolField(TEXT("receipt_persisted")));
        TestTrue(TEXT("Import and asset save outcome remains truthful"), Changing->GetBoolField(TEXT("saved")) && Changing->GetBoolField(TEXT("success")));
        TestTrue(TEXT("Replacement failure keeps old intent intact"), FMD5Hash::HashFile(*Filename) == Before);
        TestTrue(TEXT("Failed terminal publication is not evictable"), CompletedReceipts.IsEmpty());
        Request->SetStringField(TEXT("request_id"), ChangingId);
        TestEqual(TEXT("In-memory final outcome retained"), Status(Request)->GetStringField(TEXT("state")), FString(TEXT("completed")));
        Receipts.Remove(ChangingId);
        TestEqual(TEXT("Cold read of old intent is unknown, not success"), Status(Request)->GetStringField(TEXT("state")), FString(TEXT("interrupted_unknown")));
        Receipts.Add(ChangingId, Changing);
        TestTrue(TEXT("Restore writable receipt"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false));
    }
#endif
    Changing->SetStringField(TEXT("state"), TEXT("completed")); Changing->SetBoolField(TEXT("success"), true);
    PersistTerminal(ChangingId, Changing);
    TestTrue(TEXT("Final publish succeeds"), Changing->GetBoolField(TEXT("receipt_persisted")));
    TestFalse(TEXT("Recovered publication removes stale warning"), Changing->HasField(TEXT("persistence_error")));
    FString Json; TSharedPtr<FJsonObject> Disk;
    TestTrue(TEXT("Load finalized receipt"), FFileHelper::LoadFileToString(Json, *Filename) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Disk));
    if (Disk) TestTrue(TEXT("Disk has current persistence marker"), Disk->GetBoolField(TEXT("receipt_persisted")));
    TestTrue(TEXT("Corrupt finalized receipt fixture"), FFileHelper::SaveStringToFile(TEXT("{"), *Filename));
    for (int32 Index = Receipts.Num(); Index < MaxCachedReceipts; ++Index) Seed(FString::Printf(TEXT("_unknown_%d"), Index), TEXT("interrupted_unknown"));
    TestFalse(TEXT("Changed/deleted receipt file cannot authorize eviction"), MakeReceiptRoom());
    TestTrue(TEXT("Cached final receipt retained when disk corrupt"), Receipts.Contains(ChangingId));
    Receipts.Remove(ChangingId); Request->SetStringField(TEXT("request_id"), ChangingId);
    const auto Corrupt = Import(Request);
    TestEqual(TEXT("Corrupt journal cannot be reimported"), Corrupt->GetStringField(TEXT("error_code")), FString(TEXT("receipt_unavailable")));
    TestFalse(TEXT("Unknown receipt does not claim rollback"), Corrupt->HasField(TEXT("modified")) || Corrupt->HasField(TEXT("saved")));
    TestTrue(TEXT("Write syntactically valid but incomplete journal"), FFileHelper::SaveStringToFile(TEXT("{}"), *Filename));
    TestEqual(TEXT("Missing state handled safely"), Status(Request)->GetStringField(TEXT("state")), FString(TEXT("interrupted_unknown")));
    TestEqual(TEXT("Storage checks never invoke importer"), ImportDispatchCount, Dispatches);
    return !HasAnyErrors();
}
#endif
}