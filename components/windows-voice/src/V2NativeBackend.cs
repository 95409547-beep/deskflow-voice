// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Diagnostics;
using System.Text.Json;

namespace DeskflowVoice.Windows;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class V2NativeBackend(V2Options options, Action<object> log) : IV2VoiceBackend
{
    private CurrentVoiceService? service;
    private RpcLibrary? rpc;
    private V2WaveOutput? output;
    private HotkeyPattern.Target? target;
    private Mutex? lease;
    private bool ownsLease, ownedStart, stopAttempted, unknown, quarantined;
    private long ownedMicStart, startBaseline, startAttempt;
    public bool Ready => !quarantined && !unknown;

    public void Begin(V2Begin begin)
    {
        if (!Ready || ownedStart) throw new V2BusyException("backend_not_idle");
        lease = new Mutex(false, @"Local\DeskflowVoice.NativeSession");
        try { ownsLease = lease.WaitOne(0); } catch (AbandonedMutexException) { ownsLease = true; }
        if (!ownsLease) { lease.Dispose(); lease = null; throw new V2BusyException("another_product_session"); }
        try
        {
            LegacyIdle();
            service = CurrentVoiceService.Prepare(options.InputMethod, requireIdle: false);
            if (!service.ReadMicrophoneUsage().Ended) throw new V2BusyException("microphone_busy");
            var window = Os.GetForegroundWindow(); Os.GetWindowThreadProcessId(window, out var pid);
            var snapshot = new RepeatRound(1, Stopwatch.GetTimestamp(), pid, (long)window, DateTime.UtcNow);
            target = ForegroundTargets.Bind(snapshot, Environment.ProcessId, ProcessIdentity.TryRead, Owner);
            output = new V2WaveOutput(options.OutputDevicePrefix);
            rpc = RpcLibrary.LoadPinned(service.RpcPath);
            stopAttempted = false; ownedMicStart = 0;
            GuardTarget(); service.ValidateIdleAndPipe(); LegacyIdle();
            Invoke("focus-in", () => { rpc.Focus(RpcAbiAssumption.PipePath); return null; });
            RequireZero("activate", 1004, 0);
            // Stable idle checks give an already-arriving legacy gesture priority.
            Thread.Sleep(100); GuardTarget(); LegacyIdle(); service.ValidateIdleAndPipe();
            startBaseline = service.ReadMicrophoneUsage().LastStart; startAttempt = DateTime.UtcNow.ToFileTimeUtc();
            RequireZero("start", 1007, 0); ownedStart = true;
            RequireZero("show", 1012, 1);
            var wait = Stopwatch.StartNew(); var fresh = false;
            while (wait.ElapsedMilliseconds < 1500)
            {
                GuardTarget(); LegacyIdle(); service.ValidateIdentity();
                var mic = service.ReadMicrophoneUsage();
                if (mic.LastStart != startBaseline && mic.LastStart >= startAttempt && mic.LastStop < mic.LastStart) { ownedMicStart = mic.LastStart; fresh = true; break; }
                Thread.Sleep(20);
            }
            if (!fresh) throw new InvalidOperationException("Fresh microphone start not confirmed");
            log(new { type = "native_started", begin.Session, target.Pid, Hwnd = (long)target.Hwnd, target.CreatedUtc,
                ServicePid = service.Pin.Pid, MicrophoneStarted = true });
        }
        catch (Exception error)
        {
            if (ownedStart && !unknown) { try { End("begin_failed", false); } catch { quarantined = true; } }
            else Release();
            if (error is V2BusyException) throw;
            if (error is RepeatRpcUnknownException) quarantined = true;
            throw;
        }
    }
    public void Audio(byte[] pcm)
    {
        if (!ownedStart || output is null) throw new InvalidOperationException("No owned capture");
        GuardTarget(); service!.ValidateIdentity(); output.Write(pcm);
    }
    public string? CancellationReason()
    {
        if (!ownedStart) return null;
        service!.ValidateIdentity();
        var mic = service.ReadMicrophoneUsage();
        if (ownedMicStart != 0 && mic.LastStart != ownedMicStart) return "microphone_ownership_changed";
        if (mic.Ended) return "microphone_ended_externally";
        ForegroundTargets.Validate(target!, ProcessIdentity.TryRead, Owner);
        if ((long)Os.GetForegroundWindow() != (long)target!.Hwnd) return "foreground_changed";
        try { LegacyIdle(); } catch (V2BusyException) { return "legacy_session_active"; }
        return null;
    }
    public void End(string reason, bool drain)
    {
        Exception? failure = null;
        try
        {
            if (drain)
            {
                try { GuardTarget(); }
                catch (RepeatTargetRejectedException) { drain = false; reason = "foreground_changed"; }
            }
            if (drain) { try { output?.Drain(); } catch (Exception error) { failure = error; } }
            else { try { output?.Reset(); } catch (Exception error) { failure = error; } }
            if (ownedStart && !stopAttempted)
            {
                if (unknown) throw new RepeatRpcUnknownException("Native result unknown; no further RPC");
                stopAttempted = true; service!.ValidateIdentity();
                var current = service.ReadMicrophoneUsage();
                // A successful Start followed by a failed Show still needs a fresh
                // microphone identity before cleanup may issue Stop.
                if (ownedMicStart == 0)
                {
                    var confirm = Stopwatch.StartNew();
                    while (current.LastStart == startBaseline && confirm.ElapsedMilliseconds < 1500)
                    { service.ValidateIdentity(); Thread.Sleep(20); current = service.ReadMicrophoneUsage(); }
                    if (current.LastStart == startBaseline || current.LastStart < startAttempt)
                        throw new InvalidOperationException("Microphone ownership not confirmed; no Stop issued");
                    ownedMicStart = current.LastStart;
                }
                if (current.LastStart != ownedMicStart)
                    throw new InvalidOperationException("Microphone ownership changed; do not stop another capture");
                if (current.Ended)
                {
                    ownedStart = false;
                    log(new { type = "native_already_ended", reason, StopCalls = 0, MicrophoneEnded = true });
                    if (failure is not null) throw failure;
                    return;
                }
                var result = Invoke("stop", () => rpc!.Message(RpcAbiAssumption.PipePath, 1008, 0, 0));
                if (result != 0) throw new InvalidOperationException("Native stop failed");
                var wait = Stopwatch.StartNew();
                while (!service.ReadMicrophoneUsage().Ended && wait.ElapsedMilliseconds < 1500) { service.ValidateIdentity(); Thread.Sleep(20); }
                if (!service.ReadMicrophoneUsage().Ended) throw new InvalidOperationException("Microphone end not confirmed");
                ownedStart = false;
                log(new { type = "native_ended", reason, StopCalls = 1, MicrophoneEnded = true });
            }
            if (failure is not null) throw failure;
        }
        catch { quarantined = true; throw; }
        finally { Release(); }
    }
    private void GuardTarget()
    {
        ForegroundTargets.Validate(target!, ProcessIdentity.TryRead, Owner);
        if (Os.GetForegroundWindow() != target!.Hwnd) throw new RepeatTargetRejectedException("foreground_changed");
    }
    private static uint Owner(long window) { Os.GetWindowThreadProcessId((nint)window, out var pid); return pid; }
    private void RequireZero(string label, int code, int first)
    {
        service!.ValidateIdentity(); GuardTarget();
        if (Invoke(label, () => rpc!.Message(RpcAbiAssumption.PipePath, code, first, 0)) != 0)
            throw new InvalidOperationException("Native " + label + " failed");
    }
    private int? Invoke(string label, Func<int?> call)
    {
        if (unknown) throw new RepeatRpcUnknownException("No RPC after unknown result");
        var phase = 0; var time = Stopwatch.StartNew();
        using var timer = new Timer(_ =>
        {
            if (Interlocked.CompareExchange(ref phase, 2, 0) != 0) return;
            unknown = true;
            new Thread(() => { Thread.Sleep(250); Process.GetCurrentProcess().Kill(); }) { IsBackground = true }.Start();
            try { log(new { type = "native_timeout_unknown", call = label, FurtherRpc = false }); }
            finally { Process.GetCurrentProcess().Kill(); }
        }, null, 1000, Timeout.Infinite);
        log(new { type = "native_call", call = label });
        try
        {
            var result = call();
            if (Interlocked.CompareExchange(ref phase, 1, 0) != 0 || time.ElapsedMilliseconds >= 1000)
                throw new RepeatRpcUnknownException("Native return exceeded deadline");
            log(new { type = "native_return", call = label, result }); return result;
        }
        catch { Interlocked.CompareExchange(ref phase, 1, 0); unknown = true; throw; }
    }
    private void LegacyIdle()
    {
        if (options.LegacyReceiverStatus is not null)
        {
            try
            {
                using var status = JsonDocument.Parse(File.ReadAllText(options.LegacyReceiverStatus));
                var root = status.RootElement;
                if (!root.GetProperty("running").GetBoolean() ||
                    root.GetProperty("session").ValueKind != JsonValueKind.Null || root.GetProperty("cancelled_session").ValueKind != JsonValueKind.Null ||
                    DateTime.UtcNow - root.GetProperty("utc").GetDateTime().ToUniversalTime() > TimeSpan.FromSeconds(10)) throw new V2BusyException("legacy_receiver_busy");
            }
            catch { throw new V2BusyException("legacy_receiver_not_idle"); }
        }
        if (options.LegacyControllerStatus is not null)
        {
            try
            {
                using var status = JsonDocument.Parse(File.ReadAllText(options.LegacyControllerStatus));
                var root = status.RootElement; var state = root.GetProperty("State").GetString();
                if (state != "ready" && !((state is "waiting-for-next-down" or "round-completed") && root.GetProperty("Data").GetProperty("ReadyForNext").GetBoolean()))
                    throw new V2BusyException("legacy_controller_busy");
            }
            catch { throw new V2BusyException("legacy_controller_not_idle"); }
        }
    }
    private void Release()
    {
        output?.Dispose(); output = null; rpc?.Dispose(); rpc = null; target = null;
        if (ownsLease) { lease!.ReleaseMutex(); ownsLease = false; }
        lease?.Dispose(); lease = null;
    }
    public void Dispose()
    {
        if (ownedStart && !stopAttempted && !unknown) { try { End("connection_closed", false); } catch { } }
        Release();
    }
}
