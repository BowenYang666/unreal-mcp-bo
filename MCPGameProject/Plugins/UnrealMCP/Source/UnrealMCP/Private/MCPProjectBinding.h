#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealMCPProjectBinding
{
    inline bool ValidProject(const FString& Path)
    {
        if (Path.IsEmpty() || Path.Len() > 4096 || FPaths::IsRelative(Path) || !Path.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase)) return false;
        for (const TCHAR Character : Path) if (Character == 0) return false;
        return true;
    }

    inline FString NormalizeProject(FString Path)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
        FPaths::NormalizeFilename(Path);
        FPaths::CollapseRelativeDirectories(Path);
#if PLATFORM_WINDOWS
        Path.ToLowerInline();
#endif
        return Path;
    }

    inline bool ResolvePort(const FString& Config, const FString& Environment, const FString& CommandLine, int32& Port)
    {
        const FString Value = !CommandLine.IsEmpty() ? CommandLine : !Environment.IsEmpty() ? Environment : !Config.IsEmpty() ? Config : TEXT("13090");
        for (const TCHAR Character : Value) if (Character < '0' || Character > '9') return false;
        return LexTryParseString(Port, *Value) && Port >= 1 && Port <= 65535;
    }

    inline TSharedRef<FJsonObject> Identity(bool Matched)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("contract"), 1);
        Result->SetStringField(TEXT("project_path"), NormalizeProject(FPaths::GetProjectFilePath()));
        Result->SetBoolField(TEXT("matched"), Matched);
        return Result;
    }

    inline FString Serialize(const TSharedRef<FJsonObject>& Object)
    {
        FString Text; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)); return Text;
    }

    inline FString Reject(const FString& Error)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false); Result->SetBoolField(TEXT("modified"), false); Result->SetBoolField(TEXT("saved"), false);
        Result->SetStringField(TEXT("stage"), TEXT("project_binding")); Result->SetStringField(TEXT("execution_state"), TEXT("not_started"));
        auto Response = MakeShared<FJsonObject>(); Response->SetStringField(TEXT("status"), TEXT("error")); Response->SetStringField(TEXT("error"), Error);
        Response->SetObjectField(TEXT("result"), Result); Response->SetObjectField(TEXT("project_binding"), Identity(false)); return Serialize(Response);
    }
}