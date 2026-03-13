#include "UnrealMCPModule.h"
#include "UnrealMCPBridge.h"
#include "MCPLogCapture.h"
#include "Modules/ModuleManager.h"
#include "EditorSubsystem.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FUnrealMCPModule"

void FUnrealMCPModule::StartupModule()
{
	LogCapture = new FMCPLogCapture(2000);
	LogCapture->Register();
	UE_LOG(LogTemp, Display, TEXT("Unreal MCP Module has started"));
}

void FUnrealMCPModule::ShutdownModule()
{
	UE_LOG(LogTemp, Display, TEXT("Unreal MCP Module has shut down"));
	if (LogCapture)
	{
		delete LogCapture;
		LogCapture = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUnrealMCPModule, UnrealMCP) 