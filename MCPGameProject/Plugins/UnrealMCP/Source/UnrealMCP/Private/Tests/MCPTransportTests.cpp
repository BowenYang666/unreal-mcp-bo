#if WITH_DEV_AUTOMATION_TESTS
#include "MCPServerRunnable.h"
#include "MCPCommandQueue.h"
#include "UnrealMCPBridge.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Editor.h"
#include "HAL/RunnableThread.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SocketSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

struct FMCPTransportTestState
{
    TSharedPtr<FSocket> Listener;
    TUniquePtr<FMCPServerRunnable> Runnable;
    TUniquePtr<FRunnableThread> Thread;
    TFuture<FString> Response;

    ~FMCPTransportTestState()
    {
        if (Runnable) Runnable->Stop();
        if (Thread) Thread->WaitForCompletion();
        if (Listener) Listener->Close();
    }
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FMCPAwaitTransport, TSharedPtr<FMCPTransportTestState>, State, FAutomationTestBase*, Test);

bool FMCPAwaitTransport::Update()
{
    if (!State->Response.IsReady()) return false;
    const FString Response = State->Response.Get();
    TSharedPtr<FJsonObject> Result;
    if (Test->TestTrue(TEXT("Socket response parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response), Result)))
    {
        Test->TestEqual(TEXT("Request routed through bridge"), Result->GetStringField(TEXT("status")), FString(TEXT("success")));
        Test->TestEqual(TEXT("Ping response"), Result->GetObjectField(TEXT("result"))->GetStringField(TEXT("message")), FString(TEXT("pong")));
    }
    State->Runnable->Stop();
    State->Thread->WaitForCompletion();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPTransportSocketTest, "UnrealMCP.Transport.FragmentedSocket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPTransportSocketTest::RunTest(const FString& Parameters)
{
    auto State = MakeShared<FMCPTransportTestState>();
    ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    State->Listener = TSharedPtr<FSocket>(Sockets->CreateSocket(NAME_Stream, TEXT("MCPTestListener"), false),
        [Sockets](FSocket* Socket) { Sockets->DestroySocket(Socket); });
    if (!TestTrue(TEXT("Listener allocated"), State->Listener.IsValid())) return false;
    auto Address = Sockets->CreateInternetAddr();
    bool Valid;
    Address->SetIp(TEXT("127.0.0.1"), Valid);
    Address->SetPort(0);
    State->Listener->SetNonBlocking(true);
    if (!TestTrue(TEXT("Bind ephemeral loopback"), State->Listener->Bind(*Address))
        || !TestTrue(TEXT("Listen"), State->Listener->Listen(1))) return false;
    State->Listener->GetAddress(*Address);
    UUnrealMCPBridge* Bridge = GEditor->GetEditorSubsystem<UUnrealMCPBridge>();
    State->Runnable = MakeUnique<FMCPServerRunnable>(Bridge, State->Listener);
    State->Thread.Reset(FRunnableThread::Create(State->Runnable.Get(), TEXT("MCPTransportTest")));
    if (!TestTrue(TEXT("Server thread started"), State->Thread.IsValid())) return false;
    const int32 Port = Address->GetPort();
    State->Response = Async(EAsyncExecution::Thread, [Port]() -> FString
    {
        ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        TSharedPtr<FSocket> Socket(Subsystem->CreateSocket(NAME_Stream, TEXT("MCPTestClient"), false),
            [Subsystem](FSocket* Client) { Client->Close(); Subsystem->DestroySocket(Client); });
        if (!Socket) return TEXT("Client socket failed");
        auto Destination = Subsystem->CreateInternetAddr();
        bool ValidIp;
        Destination->SetIp(TEXT("127.0.0.1"), ValidIp);
        Destination->SetPort(Port);
        if (!Socket->Connect(*Destination)) return TEXT("Connect failed");
        Socket->SetNonBlocking(true);
        const FString Prefix = TEXT("{\"type\":\"ping\",\"params\":{\"padding\":\"");
        const FString Json = Prefix + FString::ChrN(8191 - Prefix.Len(), TEXT('x'))
            + TEXT("\\u4e2d") + FString::ChrN(25000, TEXT('x')) + TEXT("\"}}");
        FTCHARToUTF8 Encoded(*Json);
        const double Deadline = FPlatformTime::Seconds() + 15.0;
        int32 Offset = 0;
        while (Offset < Encoded.Length() && FPlatformTime::Seconds() < Deadline)
        {
            if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(100))) continue;
            int32 Sent = 0;
            const int32 Chunk = Offset == 0 ? 8192 : FMath::Min(4093, Encoded.Length() - Offset);
            if (!Socket->Send(reinterpret_cast<const uint8*>(Encoded.Get()) + Offset, Chunk, Sent)) return TEXT("Send failed");
            Offset += Sent;
        }
        TArray<uint8> Bytes;
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100))) continue;
            uint8 Buffer[4096];
            int32 Count = 0;
            if (!Socket->Recv(Buffer, sizeof(Buffer), Count) || Count <= 0) break;
            Bytes.Append(Buffer, Count);
            FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
            FString Text(Converted.Length(), Converted.Get());
            TSharedPtr<FJsonObject> Result;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result)) return Text;
        }
        return TEXT("No complete response before deadline");
    });
    ADD_LATENT_AUTOMATION_COMMAND(FMCPAwaitTransport(State, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPCommandQueueTest, "UnrealMCP.Transport.CommandQueue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCommandQueueTest::RunTest(const FString& Parameters)
{
    FMCPCommandQueue Queue;
    TArray<int32> Order;
    auto First = Queue.Submit([&]() -> FString
    {
        Order.Add(1);
        TestFalse(TEXT("Nested dispatch cannot start the next command"), Queue.RunNext(true));
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        TestEqual(TEXT("Task pumping did not nest another command"), Order.Num(), 1);
        return TEXT("first");
    });
    auto Second = Queue.Submit([&]() -> FString { Order.Add(2); return TEXT("second"); });
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    TestTrue(TEXT("Task pumping cannot execute queued editor work"), Order.IsEmpty());
    TestFalse(TEXT("Unsafe frame defers without mutation"), Queue.RunNext(false));
    TestFalse(TEXT("Deferred request still pending"), First->Future.IsReady());
    TestTrue(TEXT("First safe boundary runs one command"), Queue.RunNext(true));
    TestEqual(TEXT("First response preserved"), First->Future.Get(), FString(TEXT("first")));
    TestFalse(TEXT("One command per boundary"), Second->Future.IsReady());
    TestTrue(TEXT("Next boundary runs second command"), Queue.RunNext(true));
    TestEqual(TEXT("FIFO response"), Second->Future.Get(), FString(TEXT("second")));
    TestTrue(TEXT("FIFO execution"), Order == TArray<int32>({1, 2}));
    auto Expired = Queue.Submit([&]() -> FString { Order.Add(3); return TEXT("unexpected"); }, 0);
    TestFalse(TEXT("Expired work never starts"), Queue.RunNext(true));
    TestTrue(TEXT("Expired request completed"), Expired->Future.IsReady());
    TestTrue(TEXT("Expired receipt distinguishes not started"), Expired->Future.Get().Contains(TEXT("not_started")));
    auto Cancelled = Queue.Submit([&]() -> FString { Order.Add(4); return TEXT("unexpected"); });
    TestTrue(TEXT("Transport can cancel work that has not started"), Queue.CancelPending(Cancelled, TEXT("dispatch_timeout")));
    TestFalse(TEXT("Cannot fulfill a cancelled promise twice"), Queue.CancelPending(Cancelled, TEXT("dispatch_timeout")));
    TestTrue(TEXT("Cancellation releases waiter"), Cancelled->Future.IsReady());
    auto Stopped = Queue.Submit([&]() -> FString { Order.Add(5); return TEXT("unexpected"); });
    Queue.Stop();
    TestTrue(TEXT("Stop resolves pending promise before joining server"), Stopped->Future.IsReady());
    TestTrue(TEXT("Stop receipt"), Stopped->Future.Get().Contains(TEXT("server_stopped")));
    auto Late = Queue.Submit([]() -> FString { return TEXT("unexpected"); });
    TestTrue(TEXT("Submission racing shutdown is immediately rejected"), Late->Future.IsReady());
    TestFalse(TEXT("Stopped queue cannot execute"), Queue.RunNext(true));
    TestEqual(TEXT("Cancelled/expired/stopped commands did not mutate"), Order.Num(), 2);
    Queue.Start();
    auto Restarted = Queue.Submit([]() -> FString { return TEXT("restarted"); });
    TestTrue(TEXT("Queue supports explicit server restart"), Queue.RunNext(true));
    TestEqual(TEXT("Restarted response"), Restarted->Future.Get(), FString(TEXT("restarted")));
    TFuture<FString> ActiveWaiter;
    auto Active = Queue.Submit([&]() -> FString
    {
        Queue.Stop();
        TestTrue(TEXT("Reentrant stop releases the active waiter before a server join"), ActiveWaiter.WaitFor(FTimespan::FromSeconds(5)));
        return TEXT("finished after stop");
    });
    ActiveWaiter = Async(EAsyncExecution::ThreadPool, [Active]() { return Active->Future.Get(); });
    TestTrue(TEXT("Active command finishes without double-completing its promise"), Queue.RunNext(true));
    if (ActiveWaiter.IsReady())
    {
        const FString Receipt = ActiveWaiter.Get();
        TestTrue(TEXT("Active cancellation is outcome unknown"), Receipt.Contains(TEXT("server_stopped_during_execution")));
        TestFalse(TEXT("Active cancellation never claims no modification"), Receipt.Contains(TEXT("\"modified\"")));
        TestFalse(TEXT("Active cancellation never claims not saved"), Receipt.Contains(TEXT("\"saved\"")));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPFrameDispatchTest, "UnrealMCP.Transport.FrameDispatchSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPFrameDispatchTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("UnrealMCPNoServer")))
    {
        AddError(TEXT("Frame dispatch shutdown regression requires an isolated -UnrealMCPNoServer process"));
        return false;
    }
    auto Bridge = GEditor->GetEditorSubsystem<UUnrealMCPBridge>();
    if (!TestNotNull(TEXT("Editor Bridge"), Bridge)) return false;
    bool Executed = false;
    auto Request = Bridge->CommandQueue->Submit([&]() -> FString
    {
        Executed = true;
        return Bridge->ExecuteCommandOnGameThread(TEXT("ping"), MakeShared<FJsonObject>());
    });
    {
        TGuardValue<bool> SlowTask(GIsSlowTask, true);
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        Bridge->DrainCommands();
        TestFalse(TEXT("Slow task and task pumping cannot start queued work"), Executed);
        TestFalse(TEXT("Slow-task deferral leaves promise pending"), Request->Future.IsReady());
    }
    {
        TGuardValue<bool> Saving(GIsSavingPackage, true);
        Bridge->DrainCommands();
        TestFalse(TEXT("Saving cannot start queued work"), Executed);
    }
    Bridge->DrainCommands();
    TestTrue(TEXT("Safe boundary starts the command"), Executed);
    TestTrue(TEXT("Safe command result resolved"), Request->Future.IsReady());
    if (Request->Future.IsReady()) TestTrue(TEXT("Normal response preserved"), Request->Future.Get().Contains(TEXT("pong")));
    TestTrue(TEXT("A GT caller cannot recursively execute or deadlock"),
        Bridge->ExecuteCommand(TEXT("ping"), MakeShared<FJsonObject>()).Contains(TEXT("dispatch_requires_external_thread")));
    auto Pending = Bridge->CommandQueue->Submit([]() -> FString { return TEXT("must not execute"); });
    auto Waiter = Async(EAsyncExecution::ThreadPool, [Pending]() { return Pending->Future.Get(); });
    Bridge->StopServer();
    if (TestTrue(TEXT("Stop releases a waiting external thread without GT pumping"), Waiter.WaitFor(FTimespan::FromSeconds(5))))
        TestTrue(TEXT("Stopped request explicitly did not start"), Waiter.Get().Contains(TEXT("not_started")));
    Bridge->CommandQueue->Start();
    return !HasAnyErrors();
}
#endif