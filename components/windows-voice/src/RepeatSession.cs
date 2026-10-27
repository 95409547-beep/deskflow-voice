// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Diagnostics;

namespace DeskflowVoice.Windows;

// Managed state only. The short critical sections contain no I/O, process
// queries, RPC or waits, including when entered from the keyboard callback.
internal sealed class RepeatGate
{
    private readonly object sync = new();
    private RepeatRound? current;
    private bool keyHeld, closing;
    private long sequence;
    internal string? CloseReason { get; private set; }
    internal bool Closing { get { lock (sync) return closing; } }
    internal RepeatRound? Current { get { lock (sync) return current; } }

    internal RepeatKeyDecision Key(bool up, bool targetMatches, uint foregroundPid = 0, long foregroundHwnd = 0, DateTime? foregroundCapturedUtc = null)
    {
        lock (sync)
        {
            if (up)
            {
                if (!keyHeld || current is null) return new(false, "unpaired-up", null);
                keyHeld = false;
                current.Release(); // accepted sequence releases even across windows
                var released = current;
                ClearCompleted();
                return new(true, "accepted-up", released);
            }
            if (keyHeld) return new(true, "held-repeat", current);
            if (closing) return new(false, "listener-closing", null);
            if (current is not null)
            {
                // A fresh press during Stop is consumed, never queued for later.
                keyHeld = true;
                return new(true, "busy-down-dropped", current);
            }
            if (!targetMatches) return new(false, "target-rejected", null);
            keyHeld = true;
            current = new(++sequence, Stopwatch.GetTimestamp(), foregroundPid, foregroundHwnd, foregroundCapturedUtc);
            return new(true, "accepted-down", current);
        }
    }

    internal void Complete(RepeatRound round, bool safeForNext)
    {
        lock (sync)
        {
            if (!ReferenceEquals(current, round)) throw new InvalidOperationException("round ownership changed");
            if (!safeForNext) CloseUnderLock("round-fault");
            round.CleanupCompleted = safeForNext;
            ClearCompleted();
        }
    }

    internal void Close(string reason) { lock (sync) CloseUnderLock(reason); }
    private void CloseUnderLock(string reason) { closing = true; CloseReason ??= reason; }
    private void ClearCompleted()
    {
        if (!closing && !keyHeld && current is { Released: true, CleanupCompleted: true }) current = null;
    }
}

internal sealed class RepeatRound(long id, long acceptedTimestamp, uint foregroundPid = 0, long foregroundHwnd = 0, DateTime? foregroundCapturedUtc = null)
{
    private int released;
    internal long Id { get; } = id;
    internal long AcceptedTimestamp { get; } = acceptedTimestamp;
    internal DateTime AcceptedUtc { get; } = foregroundCapturedUtc ?? DateTime.UtcNow;
    internal uint ForegroundPid { get; } = foregroundPid;
    internal long ForegroundHwnd { get; } = foregroundHwnd;
    internal bool Released => Volatile.Read(ref released) != 0;
    internal bool CleanupCompleted { get; set; } // only under RepeatGate lock
    internal void Release() => Volatile.Write(ref released, 1);
}

internal sealed record RepeatKeyDecision(bool Consume, string Decision, RepeatRound? Round);
internal static class RepeatStopControl
{
    internal static void Check(string path, RepeatGate gate)
    {
        if (File.Exists(path)) gate.Close("manual-stop-requested");
    }
}
internal sealed class RepeatRpcUnknownException(string message, Exception? inner = null) : Exception(message, inner);
internal sealed class RepeatTargetRejectedException(string message) : Exception(message);
internal sealed record RepeatRoundResult(string Reason, bool Faulted, bool OwnedStart,
    bool StopAttempted, bool StopSucceeded, bool RpcUnknown, IReadOnlyList<string> Trace);

internal sealed record RepeatActions(
    RpcAbiAssumption.FocusIn Focus, RpcAbiAssumption.SimpleMessage Message,
    Func<string, Func<int?>, int?> Invoke,
    Action ValidateTarget, Action ValidateService, Action ValidateIdle,
    Func<RepeatRound, bool> ShouldEnd,
    Func<RepeatRound, string> WaitForEnd, Action ConfirmStopped);

internal static class RepeatSession
{
    // One worker calls this synchronously. Native calls and Stop never overlap.
    // Holding is outside Invoke, so a native-call watchdog cannot kill a normal
    // long press. All owned, known Start paths take the same serial finally.
    internal static RepeatRoundResult Run(RepeatRound round, RepeatActions actions)
    {
        var trace = new List<string>();
        var owned = false;
        var unknown = false;
        var faulted = false;
        var stopAttempted = false;
        var stopped = false;
        var reason = "released-or-closing-before-start";
        try
        {
            actions.ValidateService();
            actions.ValidateIdle();
            actions.ValidateTarget();
            if (!actions.ShouldEnd(round))
            {
                Call("focus-in", () => { actions.Focus(RpcAbiAssumption.PipePath); return null; });
                if (!actions.ShouldEnd(round))
                {
                    RequireZero("activate", 1004, 0);
                    if (!actions.ShouldEnd(round))
                    {
                        RequireZero("start", 1007, 0);
                        owned = true;
                        if (!actions.ShouldEnd(round))
                        {
                            RequireZero("show", 1012, 1);
                            reason = actions.WaitForEnd(round);
                        }
                        else reason = "released-or-closing-after-start";
                    }
                }
            }
        }
        catch (RepeatTargetRejectedException error)
        {
            // A known target/focus change is local to this gesture. Known Start
            // still takes serial Stop; release + successful cleanup can rearm.
            reason = "target-rejected:" + error.Message;
            trace.Add(reason);
        }
        catch (Exception error)
        {
            faulted = true;
            unknown |= error is RepeatRpcUnknownException;
            reason = error.GetType().Name + ":" + error.Message;
            trace.Add("fault:" + reason);
        }
        finally
        {
            if (owned && !unknown)
            {
                try
                {
                    // Do not require editor focus to stop our known session.
                    var result = Call("stop", () => actions.Message(RpcAbiAssumption.PipePath, 1008, 0, 0));
                    stopped = result == 0;
                    if (!stopped) throw new InvalidOperationException("Stop returned " + result + "; no retry");
                    actions.ConfirmStopped();
                }
                catch (Exception error)
                {
                    faulted = true;
                    unknown |= error is RepeatRpcUnknownException;
                    reason += "; cleanup:" + error.GetType().Name + ":" + error.Message;
                    trace.Add("cleanup-fault:" + error.Message);
                }
            }
            else if (unknown) trace.Add("no further RPC; outcome unknown");
        }
        return new(reason, faulted, owned, stopAttempted, stopped, unknown, trace);

        int? Call(string label, Func<int?> native)
        {
            // A changed service is a pre-call guard failure, not an attempted
            // RPC. Check at every boundary, including FocusIn and cleanup Stop.
            actions.ValidateService();
            if (label == "stop") stopAttempted = true;
            trace.Add(label + ":attempt");
            try
            {
                var result = actions.Invoke(label, native);
                trace.Add(label + ":return=" + (result?.ToString(System.Globalization.CultureInfo.InvariantCulture) ?? "void"));
                return result;
            }
            catch (Exception error)
            {
                // Exceptions at the native boundary are uncertain even if an
                // injected test backend did not use the dedicated exception.
                unknown = true;
                trace.Add(label + ":unknown");
                throw new RepeatRpcUnknownException(label + " outcome unknown", error);
            }
        }
        void RequireZero(string label, int message, int first)
        {
            actions.ValidateTarget();
            var result = Call(label, () => actions.Message(RpcAbiAssumption.PipePath, message, first, 0));
            if (result != 0) throw new InvalidOperationException(label + " returned " + result + "; no retry");
        }
    }
}
