// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace DeskflowVoice.Windows;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class HotkeyRepeat : IDisposable
{
    private const int NativeDeadlineMs = 1000;
    private const int MaximumHoldSeconds = 60;
    private readonly string artifactRoot;
    private readonly RepeatGate gate = new();
    private readonly AutoResetEvent wake = new(false);
    private readonly ManualResetEventSlim workerStart = new(false);
    private readonly ConcurrentQueue<(DateTime Utc, string State, object Data, bool Status)> logs = new();
    private readonly ConcurrentQueue<RepeatRound> work = new();
    private readonly object logSync = new();
    private readonly Stopwatch lifetime = new();
    private readonly HookCallback callback;
    private readonly RpcLibrary rpc;
    private readonly CurrentVoiceService boundService;
    private readonly Action validateService;
    private readonly Action validateIdleAndPipe;
    private readonly int durationSeconds;
    private readonly nint ownConsoleWindow = GetConsoleWindow();
    private readonly string stopControlPath;
    private nint hook;
    private uint pumpThread;
    private int callbackCount, faulted, deadlineUnknown;

    private HotkeyRepeat(RpcLibrary rpc, int durationSeconds, CurrentVoiceService service, string stateDirectory)
    {
        this.rpc = rpc; this.durationSeconds = durationSeconds; boundService = service;
        validateService = service.ValidateIdentity; validateIdleAndPipe = service.ValidateIdleAndPipe;
        artifactRoot = Path.GetFullPath(stateDirectory); Directory.CreateDirectory(artifactRoot);
        stopControlPath = Path.Combine(artifactRoot, $"controller-stop-{Environment.ProcessId}-{Guid.NewGuid():N}.request");
        callback = OnKey;
    }

    // Deliberately not wired to the M1 CLI. Runtime integration requires explicit configuration.
    internal static int RunFollowForeground(ControllerOptions options, int durationSeconds = 0)
    {
        if (durationSeconds < 0 || durationSeconds > 900 || durationSeconds is > 0 and < 5)
            throw new ArgumentOutOfRangeException(nameof(durationSeconds));
        var service = CurrentVoiceService.Prepare(options.InputMethod);
        using var rpc = RpcLibrary.LoadPinned(service.RpcPath);
        using var listener = new HotkeyRepeat(rpc, durationSeconds, service, options.ResolveStateDirectory());
        return listener.Listen();
    }

    // Handles are disposed only after the serial worker has ended.
    public void Dispose() { wake.Dispose(); workerStart.Dispose(); }

    private int Listen()
    {
        Emit("prepared", new { Pid = Environment.ProcessId, TargetMode = "gesture-foreground",
            TargetPid = (int?)null, TargetHwnd = (long?)null,
            TargetCreatedUtc = (DateTime?)null, rpc.LoadedModulePath, LoaderFlags = "0x900", StopControlPath = stopControlPath, StartSent = false });
        Flush();
        if (gate.Closing) throw new InvalidOperationException("log preparation failed; no hook installed");
        pumpThread = GetCurrentThreadId();
        Emit("installer-context", new { ThreadId = GetCurrentThreadId() });
        PeekMessageW(out _, nint.Zero, 0, 0, 0);
        var worker = new Thread(WorkerLoop) { IsBackground = true, Name = "right-alt-repeat-serial" };
        worker.Start(); // prepared before installing the callback
        var logTimer = new System.Threading.Timer(_ => Flush(), null, 0, 50);
        try
        {
            lifetime.Start();
            hook = SetWindowsHookExW(13, callback, GetModuleHandleW(null), 0);
            if (hook == nint.Zero)
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "repeat hook installation failed");
            Emit("ready", new { Pid = Environment.ProcessId, DurationSeconds = durationSeconds,
                Persistent = durationSeconds == 0,
                DeadlineUtc = durationSeconds == 0 ? (DateTime?)null : DateTime.UtcNow.AddSeconds(durationSeconds - lifetime.Elapsed.TotalSeconds),
                ControlPid = Environment.ProcessId, StopControlPath = stopControlPath,
                MaximumHoldSeconds, NativeCallDeadlineMs = NativeDeadlineMs,
                TargetMode = "gesture-foreground", BindOn = "each accepted keydown in worker",
                ServiceBinding = boundService?.Pin,
                TargetPid = (int?)null, TargetHwnd = (long?)null, TargetCreatedUtc = (DateTime?)null,
                InstallerThreadId = pumpThread, MessagePumpThreadId = GetCurrentThreadId(),
                HookRegistrationSucceeded = true, CallbackLiveness = "unproven until an Alt callback is recorded",
                StartSent = false, Mode = durationSeconds == 0
                    ? "manual-stop-repeat; one serial worker; no automatic restart"
                    : "finite-repeat; one serial worker; no automatic restart" });
            workerStart.Set();
            int received;
            while ((received = GetMessageW(out var message, nint.Zero, 0, 0)) > 0)
            {
                TranslateMessage(ref message);
                DispatchMessageW(ref message);
            }
            if (received < 0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "message pump failed");
        }
        finally
        {
            gate.Close("listener-exiting");
            workerStart.Set();
            wake.Set();
            // No DLL unload while a worker might still own a native call.
            if (!worker.Join(5000)) FatalTimeout("worker-cleanup", null, 5000);
            Unhook();
            logTimer.DisposeAsync().AsTask().GetAwaiter().GetResult();
            Flush();
        }
        return Volatile.Read(ref faulted) == 0 ? 0 : 1;
    }

    private nint OnKey(int code, nint message, nint data)
    {
        if (code < 0) return CallNextHookEx(hook, code, message, data);
        var entered = Stopwatch.GetTimestamp();
        var key = Marshal.PtrToStructure<KeyData>(data);
        if (!HotkeyPattern.IsAltCandidate(key.Vk)) return CallNextHookEx(hook, code, message, data);
        var count = Interlocked.Increment(ref callbackCount);
        var utc = DateTime.UtcNow;
        var hwnd = Os.GetForegroundWindow();
        Os.GetWindowThreadProcessId(hwnd, out var pid);
        var targetMatch = pid != 0 && pid != Environment.ProcessId && hwnd != nint.Zero && hwnd != ownConsoleWindow;
        var rejection = HotkeyPattern.RightAltRejectionReason(key.Vk, key.Scan, key.Flags, key.Extra);
        RepeatKeyDecision? decision = null;
        if (rejection is null)
        {
            CloseIfExpired();
            decision = gate.Key((key.Flags & 0x80) != 0, targetMatch, pid, (long)hwnd, utc);
            if (decision.Decision == "accepted-down")
            {
                work.Enqueue(decision.Round!);
                wake.Set();
            }
            else if (decision.Consume) wake.Set();
        }
        // Queue only modifier metadata; serialization and file I/O run elsewhere.
        Emit("alt-candidate", new { EventUtc = utc, key.Vk, RawScan = key.Scan, key.Flags, Extra = (ulong)key.Extra,
            NativeTime = key.Time, KeyUp = (key.Flags & 0x80) != 0, ForegroundPid = pid, ForegroundHwnd = (long)hwnd,
            TargetMatch = targetMatch, ConfiguredPid = (int?)null, ConfiguredHwnd = (long?)null,
            TargetMode = "gesture-foreground",
            Decision = rejection ?? decision!.Decision, Round = decision?.Round?.Id,
            CallbackNumber = count, CallbackThreadId = GetCurrentThreadId(),
            CallbackBodyMs = Stopwatch.GetElapsedTime(entered).TotalMilliseconds,
            Origin = "forwarded event pattern; injector PID unavailable" }, status: false);
        return decision?.Consume == true ? 1 : CallNextHookEx(hook, code, message, data);
    }

    private void WorkerLoop()
    {
        workerStart.Wait();
        RepeatRound? lastCompleted = null;
        try
        {
            while (true)
            {
                CheckWorkerStopControl();
                if (work.TryDequeue(out var round))
                {
                    RepeatRoundResult result;
                    try
                    {
                        HotkeyPattern.Target? bound = null;
                            Guard("bind-gesture-target", () => bound = ForegroundTargets.Bind(round, Environment.ProcessId, ProcessIdentity.TryRead, WindowOwner, (long)ownConsoleWindow), round.Id);
                        var roundTarget = bound ?? throw new RepeatTargetRejectedException("target binding returned no identity");
                        Emit("round-starting", new { Round = round.Id, roundTarget.Pid, Hwnd = (long)roundTarget.Hwnd,
                            roundTarget.CreatedUtc, roundTarget.ImagePath, Binding = "this-keydown" });
                        var actions = new RepeatActions(rpc.Focus, rpc.Message,
                        (label, call) => InvokeBounded(label, round.Id, call, NativeDeadlineMs, true),
                        () => Guard("target", () => ValidateRoundTarget(roundTarget), round.Id),
                        () => Guard("service", validateService, round.Id),
                        () => Guard("idle-pipe", validateIdleAndPipe, round.Id),
                        ShouldEnd, activeRound => WaitForEnd(activeRound, roundTarget),
                        () => Guard("confirm-stopped", ConfirmStopped, round.Id, 2500));
                        result = RepeatSession.Run(round, actions);
                    }
                    catch (RepeatTargetRejectedException error)
                    {
                        result = new("target-binding-rejected:" + error.Message, false, false, false, false, false, new[] { "No RPC attempted; no preceding target reused" });
                    }
                    if (result.Faulted) { Volatile.Write(ref faulted, 1); gate.Close(result.Reason); }
                    gate.Complete(round, !result.Faulted);
                    lastCompleted = round;
                    Emit("round-completed", new { Round = round.Id, Result = result, KeyReleased = round.Released,
                        ReadyForNext = !gate.Closing && gate.Current is null, Mic = boundService.ReadMicrophoneUsage(),
                        VisibleVoiceUi = "requires human witness; return values do not prove recognition" });
                }
                if (gate.Closing) break;
                if (lastCompleted is not null && gate.Current is null)
                {
                    Emit("waiting-for-next-down", new { PreviousRound = lastCompleted.Id, ReadyForNext = true, StartSent = false });
                    lastCompleted = null;
                }
                wake.WaitOne(25);
            }
        }
        catch (Exception error)
        {
            Volatile.Write(ref faulted, 1);
            gate.Close("worker-fault");
            Emit("listener-fault", new { Error = error.Message, NoAutomaticRetry = true });
        }
        finally
        {
            Emit("listener-stopped", new { Reason = gate.CloseReason, Faulted = Volatile.Read(ref faulted) != 0,
                NoAutomaticRestart = true, AltCallbackCount = Volatile.Read(ref callbackCount) });
            PostThreadMessageW(pumpThread, 0x12, 0, 0);
        }
    }

    private bool ShouldEnd(RepeatRound round)
    {
        CheckWorkerStopControl();
        return round.Released || gate.Closing || HoldLimitReached(round);
    }
    private static bool HoldLimitReached(RepeatRound round) =>
        Stopwatch.GetElapsedTime(round.AcceptedTimestamp).TotalSeconds >= MaximumHoldSeconds;
    private static uint WindowOwner(long hwnd) { Os.GetWindowThreadProcessId((nint)hwnd, out var pid); return pid; }
    private static bool IsForeground(HotkeyPattern.Target expected)
    {
        var hwnd = Os.GetForegroundWindow();
        return (long)hwnd == (long)expected.Hwnd && WindowOwner((long)hwnd) == (uint)expected.Pid;
    }
    private void ValidateGestureIdentity(HotkeyPattern.Target expected)
    {
        ForegroundTargets.Validate(expected, ProcessIdentity.TryRead, WindowOwner);
        validateService();
    }
    private void ValidateRoundTarget(HotkeyPattern.Target expected)
    {
        ValidateGestureIdentity(expected);
        if (!IsForeground(expected)) throw new RepeatTargetRejectedException("foreground changed; do not migrate this round");
    }
    private string WaitForEnd(RepeatRound round, HotkeyPattern.Target expected)
    {
        while (!ShouldEnd(round))
        {
            {
                Guard("holding-target-identity", () => ValidateGestureIdentity(expected), round.Id);
                if (!IsForeground(expected)) return "foreground-changed; Stop owned round and wait for release";
            }
            wake.WaitOne(25);
        }
        return round.Released ? "key-released" : gate.Closing ? "listener-closing" : "maximum-hold-60s";
    }
    private void CloseIfExpired()
    {
        if (durationSeconds > 0 && lifetime.IsRunning && lifetime.Elapsed.TotalSeconds >= durationSeconds)
            gate.Close("listener-duration-expired");
    }
    // Worker only, including the holding loop through ShouldEnd. The hook calls
    // CloseIfExpired directly and never touches the filesystem.
    private void CheckWorkerStopControl()
    {
        RepeatStopControl.Check(stopControlPath, gate);
        CloseIfExpired();
    }
    private void ConfirmStopped()
    {
        var wait = Stopwatch.StartNew();
        do
        {
            validateService();
            if (boundService.ReadMicrophoneUsage().Ended) return;
            Thread.Sleep(25);
        } while (wait.ElapsedMilliseconds < 1500);
        throw new InvalidOperationException("Stop returned but microphone end not confirmed within 1500ms; no next round");
    }

    private void Guard(string label, Action action, long round, int deadlineMs = 1000) =>
        InvokeBounded(label, round, () => { action(); return null; }, deadlineMs, false);

    private int? InvokeBounded(string label, long round, Func<int?> action, int deadlineMs, bool native)
    {
        if (Volatile.Read(ref deadlineUnknown) != 0)
            throw new RepeatRpcUnknownException("a previous call or guard timed out; no further RPC");
        // Every invocation has its own CAS-protected timer, never reused across
        // calls or held across the key-hold loop. Late timer callbacks are inert.
        var phase = 0;
        var entered = Stopwatch.GetTimestamp();
        using var deadline = new System.Threading.Timer(_ =>
        {
            if (Interlocked.CompareExchange(ref phase, 2, 0) == 0) FatalTimeout(label, round, deadlineMs);
        }, null, deadlineMs, Timeout.Infinite);
        if (native) Emit("rpc-call", new { Round = round, Call = label, DeadlineMs = deadlineMs });
        int? result;
        try { result = action(); }
        catch (Exception error)
        {
            var completedBeforeTimeout = Interlocked.CompareExchange(ref phase, 1, 0) == 0;
            if (!completedBeforeTimeout || Stopwatch.GetElapsedTime(entered).TotalMilliseconds >= deadlineMs)
                throw new RepeatRpcUnknownException(label + " threw after deadline; no further RPC", error);
            if (native) throw new RepeatRpcUnknownException(label + " threw at native boundary", error);
            throw;
        }
        var completed = Interlocked.CompareExchange(ref phase, 1, 0) == 0;
        if (!completed || Stopwatch.GetElapsedTime(entered).TotalMilliseconds >= deadlineMs)
            throw new RepeatRpcUnknownException(label + " deadline exceeded; no further RPC");
        if (native) Emit("rpc-return", new { Round = round, Call = label, Result = result, ReturnedVoid = result is null });
        return result;
    }

    private void FatalTimeout(string label, long? round, int deadlineMs)
    {
        Volatile.Write(ref deadlineUnknown, 1);
        // Arm a separate emergency exit BEFORE touching any gate/log lock or
        // Unhook. This thread never logs or waits for those locks. A blocked
        // Flush cannot postpone termination beyond this local 250ms fallback.
        var emergencyExit = new Thread(() =>
        {
            Thread.Sleep(250);
            Process.GetCurrentProcess().Kill();
        }) { IsBackground = true, Name = "repeat-timeout-exit" };
        try { emergencyExit.Start(); }
        catch { Process.GetCurrentProcess().Kill(); throw; }
        try
        {
            gate.Close("deadline-unknown");
            Volatile.Write(ref faulted, 1);
            Emit("timeout-unknown", new { Round = round, CallOrGuard = label, DeadlineMs = deadlineMs,
                FurtherRpc = false, ManualNativeStopMayBeNeeded = true, AutomaticRestart = false,
                EmergencyExitFallbackMs = 250 });
            Unhook();
            Flush();
        }
        finally { Process.GetCurrentProcess().Kill(); }
        throw new InvalidOperationException("dedicated helper termination failed");
    }
    private void Emit(string state, object data, bool status = true) => logs.Enqueue((DateTime.UtcNow, state, data, status));
    private void Flush()
    {
        lock (logSync)
        {
            try
            {
                while (logs.TryDequeue(out var item))
                {
                    var json = JsonSerializer.Serialize(new { item.Utc, item.State, item.Data });
                    File.AppendAllText(Path.Combine(artifactRoot, "right-alt-repeat.jsonl"), json + Environment.NewLine);
                    if (item.Status) File.WriteAllText(Path.Combine(artifactRoot, "right-alt-repeat-status.json"), json);
                    Console.WriteLine(json);
                }
            }
            catch (Exception error)
            {
                gate.Close("log-write-failed");
                Volatile.Write(ref faulted, 1);
                wake.Set();
                Console.Error.WriteLine("repeat-log-write-failed:" + error.Message);
            }
        }
    }
    private void Unhook()
    {
        var previous = Interlocked.Exchange(ref hook, nint.Zero);
        if (previous != nint.Zero) UnhookWindowsHookEx(previous);
    }
    [StructLayout(LayoutKind.Sequential)] private struct KeyData { public uint Vk, Scan, Flags, Time; public nuint Extra; }
    [StructLayout(LayoutKind.Sequential)] private struct Message { public nint Hwnd; public uint Id; public nuint WParam; public nint LParam; public uint Time; public int X, Y; public uint Private; }
    private delegate nint HookCallback(int code, nint message, nint data);
    [DllImport("user32.dll", SetLastError = true)] private static extern nint SetWindowsHookExW(int id, HookCallback callback, nint module, uint thread);
    [DllImport("user32.dll")] private static extern bool UnhookWindowsHookEx(nint hook);
    [DllImport("user32.dll")] private static extern nint CallNextHookEx(nint hook, int code, nint message, nint data);
    [DllImport("user32.dll", SetLastError = true)] private static extern int GetMessageW(out Message message, nint window, uint min, uint max);
    [DllImport("user32.dll")] private static extern bool PeekMessageW(out Message message, nint window, uint min, uint max, uint remove);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref Message message);
    [DllImport("user32.dll")] private static extern nint DispatchMessageW(ref Message message);
    [DllImport("user32.dll")] private static extern bool PostThreadMessageW(uint thread, uint message, nuint wParam, nint lParam);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern nint GetModuleHandleW(string? name);
    [DllImport("kernel32.dll")] private static extern nint GetConsoleWindow();
}
