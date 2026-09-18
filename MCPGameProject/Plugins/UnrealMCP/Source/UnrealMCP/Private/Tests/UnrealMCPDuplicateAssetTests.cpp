#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPDuplicatePersistedTexturesTest,
    "UnrealMCP.Assets.DuplicatePersistedTextures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPDuplicatePersistedTexturesTest::RunTest(const FString& Parameters)
{
    FString SourceRoot;
    FString DestinationRoot;
    FString AssetNames;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MCPDuplicateSourceRoot="), SourceRoot)
        || !FParse::Value(FCommandLine::Get(), TEXT("MCPDuplicateDestinationRoot="), DestinationRoot)
        || !FParse::Value(FCommandLine::Get(), TEXT("MCPDuplicateAssetNames="), AssetNames, false))
    {
        AddError(TEXT("Specify MCPDuplicateSourceRoot, MCPDuplicateDestinationRoot, and comma-separated MCPDuplicateAssetNames to compare saved textures read-only"));
        return false;
    }
    TArray<FString> Names;
    AssetNames.ParseIntoArray(Names, TEXT(","), true);
    TestTrue(TEXT("At least one texture specified"), Names.Num() > 0);
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    for (const FString& Name : Names)
    {
        const FString SourcePath = SourceRoot / Name;
        const FString DestinationPath = DestinationRoot / Name;
        TestTrue(TEXT("Source persisted: ") + Name, FPackageName::DoesPackageExist(SourcePath));
        TestTrue(TEXT("Copy persisted: ") + Name, FPackageName::DoesPackageExist(DestinationPath));
        const FAssetData SourceData = UEditorAssetLibrary::FindAssetData(SourcePath);
        const FAssetData CopyData = UEditorAssetLibrary::FindAssetData(DestinationPath);
        TestTrue(TEXT("Source remains an asset, not a redirector: ") + Name, SourceData.IsValid() && !SourceData.IsRedirector());
        TestTrue(TEXT("Copy is an asset, not a redirector: ") + Name, CopyData.IsValid() && !CopyData.IsRedirector());
        UTexture2D* Source = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(SourcePath));
        UTexture2D* Copy = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(DestinationPath));
        if (!TestNotNull(TEXT("Load source: ") + Name, Source) || !TestNotNull(TEXT("Load copy: ") + Name, Copy))
        {
            continue;
        }
        TestTrue(TEXT("Separate objects: ") + Name, Source != Copy);
        TestEqual(TEXT("Source package unchanged: ") + Name, Source->GetOutermost()->GetName(), SourcePath);
        TestEqual(TEXT("Copy package: ") + Name, Copy->GetOutermost()->GetName(), DestinationPath);
        int32 SettingsChecked = 0;
        for (TFieldIterator<FProperty> Property(Source->GetClass()); Property; ++Property)
        {
            if (!Property->HasAnyPropertyFlags(CPF_Edit)
                || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
            {
                continue;
            }
            FString SourceValue;
            FString CopyValue;
            Property->ExportText_InContainer(0, SourceValue, Source, Source, Source, PPF_None);
            Property->ExportText_InContainer(0, CopyValue, Copy, Copy, Copy, PPF_None);
            CopyValue.ReplaceInline(*Copy->GetPathName(), *Source->GetPathName());
            TestEqual(Name + TEXT(" setting ") + Property->GetName(), CopyValue, SourceValue);
            ++SettingsChecked;
        }
        const bool SameBlocks = TestEqual(TEXT("Source blocks: ") + Name, Copy->Source.GetNumBlocks(), Source->Source.GetNumBlocks());
        const bool SameLayers = TestEqual(TEXT("Source layers: ") + Name, Copy->Source.GetNumLayers(), Source->Source.GetNumLayers());
        TestTrue(TEXT("Source pixel data available: ") + Name, Source->Source.IsValid());
        int64 PixelBytesChecked = 0;
        if (SameBlocks && SameLayers && Source->Source.IsValid())
        {
            for (int32 BlockIndex = 0; BlockIndex < Source->Source.GetNumBlocks(); ++BlockIndex)
            {
                FTextureSourceBlock SourceBlock;
                FTextureSourceBlock CopyBlock;
                Source->Source.GetBlock(BlockIndex, SourceBlock);
                Copy->Source.GetBlock(BlockIndex, CopyBlock);
                TestEqual(TEXT("Block X"), CopyBlock.BlockX, SourceBlock.BlockX);
                TestEqual(TEXT("Block Y"), CopyBlock.BlockY, SourceBlock.BlockY);
                TestEqual(TEXT("Width"), CopyBlock.SizeX, SourceBlock.SizeX);
                TestEqual(TEXT("Height"), CopyBlock.SizeY, SourceBlock.SizeY);
                TestEqual(TEXT("Slices"), CopyBlock.NumSlices, SourceBlock.NumSlices);
                if (!TestEqual(TEXT("Mip count"), CopyBlock.NumMips, SourceBlock.NumMips))
                {
                    continue;
                }
                for (int32 LayerIndex = 0; LayerIndex < Source->Source.GetNumLayers(); ++LayerIndex)
                {
                    TestEqual(TEXT("Pixel format"), Copy->Source.GetFormat(LayerIndex), Source->Source.GetFormat(LayerIndex));
                    for (int32 MipIndex = 0; MipIndex < SourceBlock.NumMips; ++MipIndex)
                    {
                        TArray64<uint8> SourceBytes;
                        TArray64<uint8> CopyBytes;
                        const bool SourceRead = Source->Source.GetMipData(SourceBytes, BlockIndex, LayerIndex, MipIndex);
                        const bool CopyRead = Copy->Source.GetMipData(CopyBytes, BlockIndex, LayerIndex, MipIndex);
                        TestTrue(TEXT("Read source pixels"), SourceRead);
                        TestTrue(TEXT("Read copied pixels"), CopyRead);
                        TestTrue(TEXT("Mip pixels identical: ") + Name, SourceRead && CopyRead && SourceBytes == CopyBytes);
                        PixelBytesChecked += SourceBytes.Num();
                    }
                }
            }
        }
        TArray<FName> SourceDependencies;
        TArray<FName> CopyDependencies;
        Registry.GetDependencies(FName(*SourcePath), SourceDependencies, UE::AssetRegistry::EDependencyCategory::Package);
        Registry.GetDependencies(FName(*DestinationPath), CopyDependencies, UE::AssetRegistry::EDependencyCategory::Package);
        SourceDependencies.Sort(FNameLexicalLess());
        CopyDependencies.Sort(FNameLexicalLess());
        TestTrue(TEXT("Dependencies retained in place: ") + Name, SourceDependencies == CopyDependencies);
        TestFalse(TEXT("Source is clean: ") + Name, Source->GetOutermost()->IsDirty());
        TestFalse(TEXT("Copy is clean: ") + Name, Copy->GetOutermost()->IsDirty());
        AddInfo(FString::Printf(TEXT("%s: checked %d editable settings and %lld source pixel bytes; dependency sets compared"),
            *Name, SettingsChecked, PixelBytesChecked));
    }
    return !HasAnyErrors();
}

#endif