#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNiagaraRendererProperties;

namespace UnrealMCPNiagaraAuthoring
{
    TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> CreateEffectType(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> WriteScalability(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> SaveDraft(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> SetUserParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> BindUserInput(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> RedirectUserInput(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> BindDynamicUserInput(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> MultiplyFloatInput(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> SetMaterialBindings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> ReadMaterialParameters(const UNiagaraRendererProperties* Renderer);
}