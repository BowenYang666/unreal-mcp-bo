#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

class FMCPRequestBuffer
{
public:
    enum class EResult { Incomplete, Complete, TooLarge, Invalid };
    static constexpr int32 MaxBytes = 1024 * 1024;

    EResult Append(const uint8* Data, int32 Count, TSharedPtr<FJsonObject>& Request)
    {
        Request.Reset();
        if (Finished) return EResult::Invalid;
        if (Count < 0 || Count > MaxBytes - Bytes.Num()) return EResult::TooLarge;
        Bytes.Append(Data, Count);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const uint8 Character = Data[Index];
            if (InString)
            {
                if (Escaped) Escaped = false;
                else if (Character == '\\') Escaped = true;
                else if (Character == '"') InString = false;
                else if (Character < 0x20) return EResult::Invalid;
                continue;
            }
            if (Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n') continue;
            if (!Started)
            {
                if (Character != '{') return EResult::Invalid;
                Started = true;
            }
            else if (Closers.IsEmpty()) return EResult::Invalid;
            if (Character == '"') InString = true;
            else if (Character == '{' || Character == '[')
            {
                if (Closers.Num() >= 128) return EResult::Invalid;
                Closers.Add(Character == '{' ? '}' : ']');
            }
            else if (Character == '}' || Character == ']')
            {
                if (Closers.IsEmpty() || Closers.Last() != Character) return EResult::Invalid;
                Closers.Pop(EAllowShrinking::No);
            }
        }
        if (!Started || InString || !Closers.IsEmpty()) return EResult::Incomplete;
        Finished = true;
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
        const FString Text(Converted.Length(), Converted.Get());
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Request) && Request.IsValid()) return EResult::Complete;
        Request.Reset();
        return EResult::Invalid;
    }

private:
    TArray<uint8> Bytes;
    TArray<uint8> Closers;
    bool Started = false;
    bool InString = false;
    bool Escaped = false;
    bool Finished = false;
};