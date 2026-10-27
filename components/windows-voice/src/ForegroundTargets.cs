// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
namespace DeskflowVoice.Windows;
// Worker-only target identity operations. No app names/allowlists, text reads,
// foreground activation, or adoption of a preceding gesture's target.
[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal static class ForegroundTargets
{
    internal static HotkeyPattern.Target Bind(RepeatRound round, int ownPid,
        Func<int, ProcessIdentityResult> query, Func<long, uint> owner, long ownWindow = 0)
    {
        if (round.ForegroundPid == 0 || round.ForegroundPid > int.MaxValue || round.ForegroundHwnd == 0 || round.ForegroundPid == ownPid || (ownWindow != 0 && round.ForegroundHwnd == ownWindow))
            throw new RepeatTargetRejectedException("missing-or-own-foreground-window");
        var pid = (int)round.ForegroundPid;
        if (owner(round.ForegroundHwnd) != round.ForegroundPid)
            throw new RepeatTargetRejectedException("captured-window-owner-changed");
        var identity = query(pid);
        if (identity.Status != "read-only-identity-ok" || identity.Pid != pid || identity.CreatedUtc is null ||
            string.IsNullOrWhiteSpace(identity.ImagePath) || !Path.IsPathFullyQualified(identity.ImagePath))
            throw new RepeatTargetRejectedException("target-identity-unverified:" + identity.Status + ";win32=" + identity.Win32Error);
        if (identity.CreatedUtc.Value > round.AcceptedUtc)
            throw new RepeatTargetRejectedException("process-created-after-gesture; no PID reuse adoption");
        return new(pid, (nint)round.ForegroundHwnd, identity.CreatedUtc.Value, identity.ImagePath);
    }

    internal static void Validate(HotkeyPattern.Target expected,
        Func<int, ProcessIdentityResult> query, Func<long, uint> owner)
    {
        if (owner((long)expected.Hwnd) != (uint)expected.Pid)
            throw new RepeatTargetRejectedException("bound-window-owner-changed");
        var identity = query(expected.Pid);
        if (!HotkeyPattern.MatchesPreparedIdentity(expected.Pid, expected.CreatedUtc, expected.ImagePath, identity))
            throw new RepeatTargetRejectedException("bound-target-changed-or-unreadable:" + identity.Status + ";win32=" + identity.Win32Error);
    }
}
