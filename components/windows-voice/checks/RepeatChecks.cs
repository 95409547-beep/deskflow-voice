// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
namespace DeskflowVoice.Windows;
// Managed fakes only. No installed DLL, keyboard hook, OS input or microphone.
[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal static class RepeatChecks
{
    internal static Dictionary<string, bool> Run()
    {
        var checks = new Dictionary<string, bool>();

        // Two complete cycles on the same gate, including a held-key repeat.
        var consecutive = new Fixture();
        for (var index = 0; index < 2; index++)
        {
            var down = consecutive.Gate.Key(false, true);
            var repeated = consecutive.Gate.Key(false, true);
            var result = consecutive.Run(down.Round!);
            consecutive.Gate.Complete(down.Round!, !result.Faulted);
            checks[$"cycle_{index + 1}_serial_start_show_stop"] = down.Decision == "accepted-down" &&
                repeated.Decision == "held-repeat" && !result.Faulted && result.StopSucceeded &&
                result.Trace.SequenceEqual(new[] { "focus-in:attempt", "focus-in:return=void", "activate:attempt", "activate:return=0",
                    "start:attempt", "start:return=0", "show:attempt", "show:return=0", "stop:attempt", "stop:return=0" });
        }
        checks["two_rounds_no_overlap_or_extra_start"] = consecutive.Messages.SequenceEqual(new[] { 1004, 1007, 1012, 1008, 1004, 1007, 1012, 1008 }) &&
            consecutive.Gate.Current is null && !consecutive.Gate.Closing;

        // Key-up alone cannot rearm before Stop; a press during Stop is dropped
        // and must itself be released before a later, fresh down can start.
        var busy = new Fixture();
        var busyRound = busy.Gate.Key(false, true).Round!;
        RepeatKeyDecision? busyDown = null;
        busy.OnMessage = code => { if (code == 1008) busyDown = busy.Gate.Key(false, true); };
        var busyResult = busy.Run(busyRound);
        busy.Gate.Complete(busyRound, !busyResult.Faulted);
        checks["press_during_stop_dropped_not_queued"] = busyDown?.Decision == "busy-down-dropped" &&
            busy.Gate.Current == busyRound && busy.Messages.Count(m => m == 1007) == 1;
        busy.Gate.Key(true, false);
        checks["fresh_release_required_after_busy_press"] = busy.Gate.Current is null && busy.Gate.Key(false, true).Decision == "accepted-down";

        var limit = new Fixture();
        var limitedRound = limit.Gate.Key(false, true).Round!;
        limit.Wait = _ => "maximum-hold-60s"; // fake clock: no actual 60s wait
        var limitedResult = limit.Run(limitedRound);
        limit.Gate.Complete(limitedRound, !limitedResult.Faulted);
        checks["maximum_hold_stops_once_then_waits_for_real_release"] = limitedResult.StopSucceeded &&
            !limitedRound.Released && limit.Gate.Current == limitedRound &&
            limit.Gate.Key(false, true).Decision == "held-repeat" && limit.Messages.Count(m => m == 1007) == 1;
        limit.Gate.Key(true, false);
        checks["cross_window_up_releases_owned_sequence"] = limit.Gate.Current is null && limit.Gate.Key(false, true).Decision == "accepted-down";

        var expires = new Fixture();
        var expiresRound = expires.Gate.Key(false, true).Round!;
        expires.Wait = _ => { expires.Gate.Close("listener-duration-expired"); return "listener-closing"; };
        var expiresResult = expires.Run(expiresRound);
        expires.Gate.Complete(expiresRound, !expiresResult.Faulted);
        expires.Gate.Key(true, false);
        checks["expiry_while_held_serial_stop_and_no_next_round"] = expiresResult.StopSucceeded &&
            expires.Messages.Count(m => m == 1008) == 1 && expires.Gate.Closing &&
            expires.Gate.Key(false, true).Decision == "listener-closing";

        var pendingExpiry = new Fixture();
        var pendingRound = pendingExpiry.Gate.Key(false, true).Round!;
        pendingExpiry.Gate.Close("listener-duration-expired");
        var pendingResult = pendingExpiry.Run(pendingRound);
        pendingExpiry.Gate.Complete(pendingRound, !pendingResult.Faulted);
        checks["expired_pending_press_does_not_start"] = pendingExpiry.Messages.Count == 0 && !pendingResult.OwnedStart;

        var quickRelease = new Fixture();
        var quickRound = quickRelease.Gate.Key(false, true).Round!;
        quickRelease.Gate.Key(true, false);
        var quickResult = quickRelease.Run(quickRound);
        quickRelease.Gate.Complete(quickRound, !quickResult.Faulted);
        checks["released_before_worker_has_no_rpc_and_can_rearm"] = quickRelease.AttemptLabels.Count == 0 &&
            quickRelease.Gate.Current is null && quickRelease.Gate.Key(false, true).Decision == "accepted-down";

        var holding = new Fixture();
        var holdingRound = holding.Gate.Key(false, true).Round!;
        var heldWithoutStop = false;
        holding.Wait = current =>
        {
            heldWithoutStop = !current.Released && holding.Messages.SequenceEqual(new[] { 1004, 1007, 1012 }) &&
                Enumerable.Range(0, 10).All(_ => holding.Gate.Key(false, true).Decision == "held-repeat") &&
                holding.Messages.Count(m => m == 1007) == 1;
            holding.Gate.Key(true, false);
            return "key-released";
        };
        var holdingResult = holding.Run(holdingRound);
        holding.Gate.Complete(holdingRound, !holdingResult.Faulted);
        checks["held_repeats_do_not_start_or_stop_until_release"] = heldWithoutStop && holdingResult.StopSucceeded && holding.Gate.Current is null;

        foreach (var label in new[] { "focus-in", "start", "show", "stop" })
        {
            var unknown = new Fixture { UnknownAt = label };
            var unknownRound = unknown.Gate.Key(false, true).Round!;
            var result = unknown.Run(unknownRound);
            unknown.Gate.Complete(unknownRound, !result.Faulted);
            unknown.Gate.Key(true, false);
            checks[$"unknown_{label}_blocks_next_round_and_no_further_rpc"] = result.Faulted && result.RpcUnknown &&
                unknown.Gate.Closing && unknown.Gate.Key(false, true).Decision == "listener-closing" &&
                (label == "stop" ? result.StopAttempted && unknown.AttemptLabels.Last() == "stop" : !result.StopAttempted);
        }

        var stopFails = new Fixture { StopResult = 7 };
        var failedRound = stopFails.Gate.Key(false, true).Round!;
        var failedResult = stopFails.Run(failedRound);
        stopFails.Gate.Complete(failedRound, !failedResult.Faulted);
        stopFails.Gate.Key(true, false);
        checks["stop_nonzero_no_retry_and_no_next_round"] = failedResult.Faulted && !failedResult.StopSucceeded &&
            stopFails.Messages.Count(m => m == 1008) == 1 && stopFails.Gate.Key(false, true).Decision == "listener-closing";

        var serviceChanges = new Fixture();
        var serviceRound = serviceChanges.Gate.Key(false, true).Round!;
        serviceChanges.Wait = _ => { serviceChanges.ServiceChanged = true; return "key-released"; };
        var serviceResult = serviceChanges.Run(serviceRound);
        serviceChanges.Gate.Complete(serviceRound, !serviceResult.Faulted);
        serviceChanges.Gate.Key(true, false);
        checks["service_change_no_stop_to_new_service_or_next_round"] = serviceResult.Faulted && !serviceResult.StopAttempted &&
            serviceChanges.Gate.Key(false, true).Decision == "listener-closing";

        var changesAfterActivate = new Fixture();
        var activationRound = changesAfterActivate.Gate.Key(false, true).Round!;
        changesAfterActivate.OnMessage = code => { if (code == 1004) changesAfterActivate.ServiceChanged = true; };
        var activationResult = changesAfterActivate.Run(activationRound);
        changesAfterActivate.Gate.Complete(activationRound, !activationResult.Faulted);
        changesAfterActivate.Gate.Key(true, false);
        checks["service_changes_after_activate_no_start_or_further_rpc"] = activationResult.Faulted && !activationResult.OwnedStart &&
            !activationResult.StopAttempted && changesAfterActivate.Messages.SequenceEqual(new[] { 1004 }) &&
            changesAfterActivate.AttemptLabels.SequenceEqual(new[] { "focus-in", "activate" }) &&
            changesAfterActivate.Gate.Key(false, true).Decision == "listener-closing";

        var focusLoss = new Fixture();
        var focusRound = focusLoss.Gate.Key(false, true).Round!;
        focusLoss.Wait = _ => throw new InvalidOperationException("foreground changed");
        var focusResult = focusLoss.Run(focusRound);
        focusLoss.Gate.Complete(focusRound, !focusResult.Faulted);
        checks["focus_loss_still_stops_owned_voice_once"] = focusResult.Faulted && focusResult.StopSucceeded &&
            focusLoss.Messages.Count(m => m == 1008) == 1;

        var micPending = new Fixture { MicEnded = false };
        var micRound = micPending.Gate.Key(false, true).Round!;
        var micResult = micPending.Run(micRound);
        micPending.Gate.Complete(micRound, !micResult.Faulted);
        micPending.Gate.Key(true, false);
        checks["stop_zero_but_mic_not_ended_blocks_next_round"] = micResult.StopSucceeded && micResult.Faulted &&
            micPending.Gate.Key(false, true).Decision == "listener-closing";

        var showFails = new Fixture { ShowResult = 3 };
        var showRound = showFails.Gate.Key(false, true).Round!;
        var showResult = showFails.Run(showRound);
        showFails.Gate.Complete(showRound, !showResult.Faulted);
        checks["show_nonzero_still_stops_owned_voice_once"] = showResult.Faulted && showResult.StopSucceeded &&
            showFails.Messages.Count(m => m == 1008) == 1 && showFails.Gate.Closing;

        var stopPath = Path.Combine(Path.GetTempPath(), "Deskflow-repeat-stop-test-" + Guid.NewGuid().ToString("N") + ".request");
        try
        {
            var idleStop = new Fixture();
            RepeatStopControl.Check(stopPath, idleStop.Gate);
            var noRequestLeavesOpen = !idleStop.Gate.Closing;
            File.WriteAllText(stopPath, "managed test");
            RepeatStopControl.Check(stopPath, idleStop.Gate);
            checks["manual_stop_file_while_idle_no_rpc_no_new_round"] = noRequestLeavesOpen && idleStop.Gate.Closing &&
                idleStop.Gate.CloseReason == "manual-stop-requested" && idleStop.Messages.Count == 0 &&
                idleStop.Gate.Key(false, true).Decision == "listener-closing";
            File.Delete(stopPath);

            var heldStop = new Fixture();
            var heldStopRound = heldStop.Gate.Key(false, true).Round!;
            heldStop.Wait = _ =>
            {
                File.WriteAllText(stopPath, "managed test");
                RepeatStopControl.Check(stopPath, heldStop.Gate);
                return "listener-closing";
            };
            var heldStopResult = heldStop.Run(heldStopRound);
            heldStop.Gate.Complete(heldStopRound, !heldStopResult.Faulted);
            heldStop.Gate.Key(true, false);
            checks["manual_stop_file_while_held_serial_stop_once_no_rearm"] = !heldStopResult.Faulted && heldStopResult.StopSucceeded &&
                heldStop.Gate.CloseReason == "manual-stop-requested" && heldStop.Messages.SequenceEqual(new[] { 1004, 1007, 1012, 1008 }) &&
                heldStop.Gate.Key(false, true).Decision == "listener-closing";
        }
        finally { File.Delete(stopPath); }

        foreach (var item in ForegroundModeChecks.Run()) checks.Add(item.Key, item.Value);
        return checks;
    }

    private sealed class Fixture
    {
        internal RepeatGate Gate { get; } = new();
        internal List<int> Messages { get; } = new();
        internal List<string> AttemptLabels { get; } = new();
        internal Action<int>? OnMessage { get; set; }
        internal Func<RepeatRound, string>? Wait { get; set; }
        internal string? UnknownAt { get; init; }
        internal int StopResult { get; init; }
        internal int ShowResult { get; init; }
        internal bool ServiceChanged { get; set; }
        internal bool MicEnded { get; init; } = true;
        internal RepeatRoundResult Run(RepeatRound round) => RepeatSession.Run(round, new(
            _ => { },
            (_, message, _, _) => { Messages.Add(message); OnMessage?.Invoke(message); return message == 1008 ? StopResult : message == 1012 ? ShowResult : 0; },
            (label, call) => { AttemptLabels.Add(label); if (UnknownAt == label) throw new RepeatRpcUnknownException("simulated native timeout"); return call(); },
            () => { },
            () => { if (ServiceChanged) throw new InvalidOperationException("service changed"); },
            () => { },
            current => current.Released || Gate.Closing,
            current => { if (Wait is not null) return Wait(current); Gate.Key(true, true); return "key-released"; },
            () => { if (!MicEnded) throw new InvalidOperationException("microphone still active"); }));
    }
}
