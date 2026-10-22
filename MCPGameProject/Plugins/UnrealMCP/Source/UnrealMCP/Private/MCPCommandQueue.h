#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

class FMCPCommandQueue
{
public:
    enum class EState { Pending, Executing, Complete, Cancelled };

    struct FRequest
    {
        FRequest(TUniqueFunction<FString()>&& InWork, double InDeadline)
            : Work(MoveTemp(InWork)), Future(Promise.GetFuture()), Deadline(InDeadline) {}

        TUniqueFunction<FString()> Work;
        TPromise<FString> Promise;
        TFuture<FString> Future;
        double Deadline;
        EState State = EState::Pending;
        bool ResponseDelivered = false;
    };

    using FTicket = TSharedRef<FRequest, ESPMode::ThreadSafe>;

    ~FMCPCommandQueue() { Stop(); }

    static FString Rejection(const FString& Code, const FString& Message, bool NotStarted = true)
    {
        auto Receipt = MakeShared<FJsonObject>();
        Receipt->SetBoolField(TEXT("success"), false);
        if (NotStarted)
        {
            Receipt->SetBoolField(TEXT("modified"), false);
            Receipt->SetBoolField(TEXT("saved"), false);
        }
        Receipt->SetStringField(TEXT("stage"), TEXT("dispatch"));
        Receipt->SetStringField(TEXT("error_code"), Code);
        Receipt->SetStringField(TEXT("execution_state"), NotStarted ? TEXT("not_started") : TEXT("unknown"));
        Receipt->SetStringField(TEXT("error"), Message);
        auto Response = MakeShared<FJsonObject>();
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("error"), Message);
        Response->SetObjectField(TEXT("result"), Receipt);
        FString Json;
        FJsonSerializer::Serialize(Response, TJsonWriterFactory<>::Create(&Json));
        return Json;
    }

    FTicket Submit(TUniqueFunction<FString()>&& Work, double QueueTimeoutSeconds = 20.0)
    {
        auto Request = MakeShared<FRequest, ESPMode::ThreadSafe>(MoveTemp(Work), FPlatformTime::Seconds() + QueueTimeoutSeconds);
        FScopeLock Lock(&Mutex);
        if (!Accepting || Pending.Num() >= 64)
        {
            CancelLocked(Request, !Accepting ? TEXT("server_stopped") : TEXT("dispatch_queue_full"));
        }
        else Pending.Add(Request);
        return Request;
    }

    bool CancelPending(const FTicket& Request, const FString& Code)
    {
        FScopeLock Lock(&Mutex);
        if (Request->State != EState::Pending) return false;
        Pending.Remove(Request);
        CancelLocked(Request, Code);
        return true;
    }

    bool RunNext(bool SafeToExecute)
    {
        check(IsInGameThread());
        TSharedPtr<FRequest, ESPMode::ThreadSafe> Request;
        {
            FScopeLock Lock(&Mutex);
            if (!Accepting || Executing || !SafeToExecute) return false;
            while (!Pending.IsEmpty())
            {
                Request = Pending[0];
                Pending.RemoveAt(0);
                if (FPlatformTime::Seconds() >= Request->Deadline)
                {
                    CancelLocked(Request.ToSharedRef(), TEXT("dispatch_timeout"));
                    Request.Reset();
                    continue;
                }
                Request->State = EState::Executing;
                ActiveRequest = Request;
                Executing = true;
                break;
            }
        }
        if (!Request) return false;
        FString Result = Request->Work();
        {
            FScopeLock Lock(&Mutex);
            Request->Work = nullptr;
            Request->State = EState::Complete;
            if (!Request->ResponseDelivered)
            {
                Request->Promise.SetValue(MoveTemp(Result));
                Request->ResponseDelivered = true;
            }
            ActiveRequest.Reset();
            Executing = false;
        }
        return true;
    }

    void Start()
    {
        FScopeLock Lock(&Mutex);
        Accepting = true;
    }

    void Stop()
    {
        FScopeLock Lock(&Mutex);
        Accepting = false;
        for (const auto& Request : Pending) CancelLocked(Request, TEXT("server_stopped"));
        Pending.Reset();
        if (ActiveRequest && !ActiveRequest->ResponseDelivered)
        {
            ActiveRequest->Promise.SetValue(Rejection(TEXT("server_stopped_during_execution"),
                TEXT("Server stopped after command started; final mutation/save outcome unknown. Inspect before retrying"), false));
            ActiveRequest->ResponseDelivered = true;
        }
    }

private:
    void CancelLocked(const FTicket& Request, const FString& Code)
    {
        Request->State = EState::Cancelled;
        Request->Work = nullptr;
        Request->Promise.SetValue(Rejection(Code, TEXT("Command did not start; editor dispatch unavailable or queue deadline exceeded")));
        Request->ResponseDelivered = true;
    }

    FCriticalSection Mutex;
    TArray<FTicket> Pending;
    TSharedPtr<FRequest, ESPMode::ThreadSafe> ActiveRequest;
    bool Accepting = true;
    bool Executing = false;
};