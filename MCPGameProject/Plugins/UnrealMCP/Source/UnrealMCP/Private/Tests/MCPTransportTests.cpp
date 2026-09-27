#if WITH_DEV_AUTOMATION_TESTS
#include "MCPServerRunnable.h"
#include "UnrealMCPBridge.h"
#include "Async/Async.h"
#include "Editor.h"
#include "HAL/RunnableThread.h"
#include "Misc/AutomationTest.h"
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
#endif