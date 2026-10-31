#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;
class UNiagaraEffectType;
class UNiagaraRendererProperties;
struct FNiagaraEmitterHandle;

namespace UnrealMCPNiagaraScalability
{
    TSharedRef<FJsonObject> System(const UNiagaraSystem* Asset);
    TSharedRef<FJsonObject> Emitter(const FNiagaraEmitterHandle& Handle);
    TSharedRef<FJsonObject> Renderer(const UNiagaraRendererProperties* Asset);
    TSharedRef<FJsonObject> EffectType(const UNiagaraEffectType* Asset);
    TSharedPtr<FJsonObject> ReadEffectTypeAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedRef<FJsonObject> Context();

    struct FReader
    {
        TSharedRef<FJsonObject> Structure(const UStruct* Type, const void* Data, const FString& Path = TEXT("saved"), int32 Depth = 0);
        TSharedRef<FJsonObject> Properties(const UObject* Object, const TArray<FName>& Names, const FString& Path);
        TSharedRef<FJsonObject> Finish(const TSharedRef<FJsonObject>& Result);
        void Missing(const FString& Path, const FString& Reason);

        TArray<TSharedPtr<FJsonValue>> Unsupported;
        TSharedRef<FJsonObject> FieldStates = MakeShared<FJsonObject>();
        TSharedRef<FJsonObject> DefaultMatches = MakeShared<FJsonObject>();
        int32 Values = 0;

    private:
        TSharedPtr<FJsonValue> Value(const FProperty* Property, const void* Data, const FString& Path, int32 Depth);
    };
}
