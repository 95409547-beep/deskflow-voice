// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
namespace DeskflowVoice.Windows;
[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal static class ForegroundModeChecks
{
    private static readonly DateTime Created = new(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
    private static readonly ProcessIdentityResult AppA = new(101, @"C:\Apps\One.exe", Created, null, "read-only-identity-ok");
    private static readonly ProcessIdentityResult AppB = new(202, @"C:\Apps\Two.exe", Created, null, "read-only-identity-ok");

    internal static Dictionary<string, bool> Run()
    {
        var checks = new Dictionary<string, bool>();
        var run = new Fixture();
        var first = run.Accept(101, 1001);
        var firstTarget = run.Bind(first);
        var firstResult = run.Run(first, firstTarget);
        run.Gate.Complete(first, !firstResult.Faulted);
        var second = run.Accept(202, 2002);
        var secondTarget = run.Bind(second);
        var secondResult = run.Run(second, secondTarget);
        run.Gate.Complete(second, !secondResult.Faulted);
        checks["foreground_two_apps_two_serial_rounds_rebind_without_registration"] =
            firstTarget.Pid == 101 && secondTarget.Pid == 202 && firstResult.StopSucceeded && secondResult.StopSucceeded &&
            run.Starts.SequenceEqual(new[] { 101, 202 }) && run.Stops == 2 && !run.Gate.Closing && run.Gate.Current is null;

        var moving = new Fixture();
        var movingRound = moving.Accept(101, 1001);
        var movingTarget = moving.Bind(movingRound);
        moving.Wait = _ => { moving.ForegroundPid = 202; moving.ForegroundHwnd = 2002; return "foreground-changed; wait for release"; };
        var movingResult = moving.Run(movingRound, movingTarget);
        moving.Gate.Complete(movingRound, !movingResult.Faulted);
        var remainsBound = moving.Gate.Current == movingRound && movingRound.ForegroundPid == 101;
        moving.Gate.Key(true, false, 202, 2002);
        var afterMove = moving.Accept(202, 2002);
        checks["foreground_change_stops_once_keeps_listener_then_accepts_new_app"] =
            !movingResult.Faulted && movingResult.StopSucceeded && remainsBound && !moving.Gate.Closing && moving.Bind(afterMove).Pid == 202;

        var beforeShow = new Fixture { SwitchAfterStart = true };
        var beforeShowRound = beforeShow.Accept(101, 1001);
        var beforeShowResult = beforeShow.Run(beforeShowRound, beforeShow.Bind(beforeShowRound));
        beforeShow.Gate.Complete(beforeShowRound, !beforeShowResult.Faulted);
        beforeShow.Gate.Key(true, false, 202, 2002);
        checks["target_change_after_start_before_show_uses_stop_and_can_rearm"] =
            !beforeShowResult.Faulted && beforeShowResult.StopSucceeded && !beforeShowResult.Trace.Contains("show:attempt") &&
            beforeShow.Gate.Key(false, true, 202, 2002).Decision == "accepted-down";

        var moveStopFails = new Fixture { StopResult = 8 };
        var moveFailureRound = moveStopFails.Accept(101, 1001);
        moveStopFails.Wait = _ => { moveStopFails.ForegroundPid = 202; moveStopFails.ForegroundHwnd = 2002; return "foreground-changed"; };
        var moveFailureResult = moveStopFails.Run(moveFailureRound, moveStopFails.Bind(moveFailureRound));
        moveStopFails.Gate.Complete(moveFailureRound, !moveFailureResult.Faulted);
        moveStopFails.Gate.Key(true, false, 202, 2002);
        checks["known_focus_change_with_stop_failure_still_blocks_next_round"] = moveFailureResult.Faulted && !moveFailureResult.StopSucceeded &&
            moveStopFails.Gate.Closing && moveStopFails.Gate.Key(false, true, 202, 2002).Decision == "listener-closing";

        var failed = new Fixture();
        var good = failed.Accept(101, 1001);
        var goodResult = failed.Run(good, failed.Bind(good));
        failed.Gate.Complete(good, !goodResult.Faulted);
        var missing = failed.Accept(202, 2002);
        failed.Identities[202] = AppB with { ImagePath = null, CreatedUtc = null, Status = "read-denied-or-unavailable", Win32Error = 5 };
        var rejected = Rejects(() => failed.Bind(missing));
        failed.Gate.Complete(missing, true); // worker reports no-Start rejection
        failed.Gate.Key(true, false, 202, 2002);
        failed.Identities[202] = AppB;
        var retry = failed.Accept(202, 2002);
        checks["unreadable_new_target_never_reuses_previous_app_and_next_gesture_rebinds"] = rejected &&
            failed.Starts.SequenceEqual(new[] { 101 }) && failed.Bind(retry).Pid == 202 && !failed.Gate.Closing;

        var own = new Fixture();
        var ownRound = own.Accept(999, 9999);
        checks["own_helper_pid_rejected"] = Rejects(() => own.Bind(ownRound));
        var consoleRound = new RepeatRound(100, 0, 101, 9999);
        checks["own_helper_console_window_rejected"] = Rejects(() => ForegroundTargets.Bind(consoleRound, 999, _ => AppA, _ => 101, 9999));

        var birth = new Fixture();
        var birthRound = birth.Accept(101, 1001);
        birth.Identities[101] = AppA with { CreatedUtc = birthRound.AcceptedUtc.AddTicks(1) };
        checks["process_created_after_keydown_not_adopted"] = Rejects(() => birth.Bind(birthRound));

        var hosts = new Fixture();
        hosts.Identities[101] = AppA with { ImagePath = @"C:\Program Files\dotnet\dotnet.exe" };
        hosts.Identities[202] = AppB with { ImagePath = @"C:\Program Files\PowerShell\7\pwsh.exe" };
        var hostA = ForegroundTargets.Bind(new RepeatRound(1, 0, 101, 1001), 999, p => hosts.Identities[p], h => h == 1001 ? 101u : 202u);
        var hostB = ForegroundTargets.Bind(new RepeatRound(2, 0, 202, 2002), 999, p => hosts.Identities[p], h => h == 1001 ? 101u : 202u);
        checks["dotnet_and_pwsh_apps_not_excluded_by_image_name"] = hostA.Pid == 101 && hostB.Pid == 202;

        var oldService = new ProcessIdentityResult(3001, Path.Combine(Path.GetTempPath(), "FixtureVendor", "ImeService.exe"), Created, null, "read-only-identity-ok");
        var newService = oldService with { Pid = 4002, CreatedUtc = Created.AddHours(1) };
        var oldPin = CurrentVoiceService.BindUnique(new[] { oldService }, oldService.ImagePath!);
        var newPin = CurrentVoiceService.BindUnique(new[] { newService }, oldService.ImagePath!);
        var oldRejectsNew = Throws(() => CurrentVoiceService.RequireSame(oldPin, newService));
        CurrentVoiceService.RequireSame(newPin, newService);
        checks["trusted_service_normal_next_start_binds_new_pid_without_recompile"] = oldPin.Pid == 3001 && newPin.Pid == 4002 && oldRejectsNew;
        checks["trusted_service_ambiguity_rejected"] = Throws(() => CurrentVoiceService.BindUnique(new[] { oldService, newService }, oldService.ImagePath!));
        checks["trusted_service_same_pid_new_creation_rejected_mid_session"] = Throws(() => CurrentVoiceService.RequireSame(oldPin, oldService with { CreatedUtc = Created.AddTicks(1) }));
        return checks;
    }

    private static bool Rejects(Action action) { try { action(); return false; } catch (RepeatTargetRejectedException) { return true; } }
    private static bool Throws(Action action) { try { action(); return false; } catch (InvalidOperationException) { return true; } }

    private sealed class Fixture
    {
        internal readonly RepeatGate Gate = new();
        internal readonly Dictionary<int, ProcessIdentityResult> Identities = new() { [101] = AppA, [202] = AppB };
        internal readonly List<int> Starts = new();
        internal uint ForegroundPid;
        internal long ForegroundHwnd;
        internal int Stops;
        internal bool SwitchAfterStart;
        internal int StopResult;
        internal Func<RepeatRound, string>? Wait;
        internal RepeatRound Accept(uint pid, long hwnd)
        {
            ForegroundPid = pid; ForegroundHwnd = hwnd;
            return Gate.Key(false, true, pid, hwnd).Round!;
        }
        private uint Owner(long hwnd) => hwnd == 1001 ? 101u : hwnd == 2002 ? 202u : ForegroundPid;
        internal HotkeyPattern.Target Bind(RepeatRound round) => ForegroundTargets.Bind(round, 999, p => Identities[p], Owner);
        internal RepeatRoundResult Run(RepeatRound round, HotkeyPattern.Target pin) => RepeatSession.Run(round, new(
            _ => { },
            (_, code, _, _) =>
            {
                if (code == 1007) { Starts.Add(pin.Pid); if (SwitchAfterStart) { ForegroundPid = 202; ForegroundHwnd = 2002; } }
                if (code == 1008) { Stops++; return StopResult; }
                return 0;
            },
            (_, call) => call(),
            () => { ForegroundTargets.Validate(pin, p => Identities[p], Owner); if (ForegroundPid != pin.Pid || ForegroundHwnd != (long)pin.Hwnd) throw new RepeatTargetRejectedException("foreground-changed"); },
            () => { }, () => { },
            active => active.Released || Gate.Closing,
            active => { if (Wait is not null) return Wait(active); Gate.Key(true, false, ForegroundPid, ForegroundHwnd); return "key-released"; },
            () => { }));
    }
}
