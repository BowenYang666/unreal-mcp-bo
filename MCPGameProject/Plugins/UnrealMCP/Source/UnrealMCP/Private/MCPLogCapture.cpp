#include "MCPLogCapture.h"
#include "Misc/OutputDeviceRedirector.h"

FMCPLogCapture* FMCPLogCapture::Instance = nullptr;

FMCPLogCapture::FMCPLogCapture(int32 InMaxLines)
	: MaxLines(FMath::Max(InMaxLines, 100))
	, bRegistered(false)
{
	Buffer.Reserve(MaxLines);
	Instance = this;
}

FMCPLogCapture::~FMCPLogCapture()
{
	Unregister();
	if (Instance == this)
	{
		Instance = nullptr;
	}
}

FMCPLogCapture* FMCPLogCapture::Get()
{
	return Instance;
}

void FMCPLogCapture::Register()
{
	if (!bRegistered)
	{
		GLog->AddOutputDevice(this);
		bRegistered = true;
		UE_LOG(LogTemp, Display, TEXT("MCPLogCapture: Registered with GLog (buffer size: %d)"), MaxLines);
	}
}

void FMCPLogCapture::Unregister()
{
	if (bRegistered)
	{
		GLog->RemoveOutputDevice(this);
		bRegistered = false;
	}
}

void FMCPLogCapture::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category)
{
	AddEntry(Data, Verbosity, Category, FPlatformTime::Seconds());
}

void FMCPLogCapture::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category, double Time)
{
	AddEntry(Data, Verbosity, Category, Time);
}

void FMCPLogCapture::AddEntry(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category, double Time)
{
	FScopeLock ScopeLock(&Lock);

	if (Buffer.Num() >= MaxLines)
	{
		// Remove the oldest entry
		Buffer.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FLogEntry& Entry = Buffer.AddDefaulted_GetRef();
	Entry.Message = Data;
	Entry.Category = Category;
	Entry.Verbosity = Verbosity;
	Entry.Timestamp = Time;
}

int32 FMCPLogCapture::GetCapturedCount() const
{
	FScopeLock ScopeLock(&Lock);
	return Buffer.Num();
}

TArray<FMCPLogCapture::FLogEntry> FMCPLogCapture::GetLogs(
	int32 Count,
	ELogVerbosity::Type MinVerbosity,
	const FString& CategoryFilter,
	const FString& SearchText) const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FLogEntry> Result;

	const bool bFilterCategory = !CategoryFilter.IsEmpty();
	const bool bFilterSearch = !SearchText.IsEmpty();
	const FName CategoryName = bFilterCategory ? FName(*CategoryFilter) : NAME_None;

	// Iterate from newest to oldest (end to start)
	for (int32 i = Buffer.Num() - 1; i >= 0; --i)
	{
		const FLogEntry& Entry = Buffer[i];

		// Filter by verbosity: only include entries at MinVerbosity or more severe
		// ELogVerbosity: Fatal=1, Error=2, Warning=3, Display=4, Log=5, Verbose=6, VeryVerbose=7
		// Lower number = more severe. All=7.
		if (MinVerbosity != ELogVerbosity::All && Entry.Verbosity > MinVerbosity)
		{
			continue;
		}

		// Filter by category
		if (bFilterCategory && Entry.Category != CategoryName)
		{
			continue;
		}

		// Filter by search text (case-insensitive)
		if (bFilterSearch && !Entry.Message.Contains(SearchText, ESearchCase::IgnoreCase))
		{
			continue;
		}

		Result.Add(Entry);

		if (Count > 0 && Result.Num() >= Count)
		{
			break;
		}
	}

	// Reverse so oldest is first, newest is last
	Algo::Reverse(Result);

	return Result;
}
