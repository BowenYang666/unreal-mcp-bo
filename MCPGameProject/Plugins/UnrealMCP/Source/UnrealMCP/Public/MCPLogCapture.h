#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

/**
 * Custom FOutputDevice that captures UE log output into a ring buffer.
 * Registered with GLog on plugin startup to capture all UE_LOG messages.
 * Thread-safe: Serialize() can be called from any thread.
 */
class UNREALMCP_API FMCPLogCapture : public FOutputDevice
{
public:
	FMCPLogCapture(int32 InMaxLines = 2000);
	virtual ~FMCPLogCapture();

	// FOutputDevice interface
	virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category, double Time) override;
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	/** Register this device with GLog. Call once during plugin startup. */
	void Register();

	/** Unregister from GLog. Call during plugin shutdown. */
	void Unregister();

	struct FLogEntry
	{
		FString Message;
		FName Category;
		ELogVerbosity::Type Verbosity;
		double Timestamp;
	};

	/**
	 * Get recent log entries.
	 * @param Count       Max number of entries to return (0 = all captured)
	 * @param MinVerbosity Minimum verbosity level to include (e.g. ELogVerbosity::Warning returns Warning+Error+Fatal)
	 * @param CategoryFilter If non-empty, only include entries matching this category name
	 * @param SearchText  If non-empty, only include entries whose message contains this substring (case-insensitive)
	 */
	TArray<FLogEntry> GetLogs(int32 Count = 0,
	                          ELogVerbosity::Type MinVerbosity = ELogVerbosity::All,
	                          const FString& CategoryFilter = TEXT(""),
	                          const FString& SearchText = TEXT("")) const;

	/** Get the total number of captured log entries currently in the buffer. */
	int32 GetCapturedCount() const;

	/** Singleton accessor - created by the module. */
	static FMCPLogCapture* Get();

private:
	mutable FCriticalSection Lock;
	TArray<FLogEntry> Buffer;
	int32 MaxLines;
	bool bRegistered;

	static FMCPLogCapture* Instance;

	void AddEntry(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category, double Time);
};
