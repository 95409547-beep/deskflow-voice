// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
namespace DeskflowVoice.Windows;

// Metadata/pattern helpers extracted from the authored prototype, without its
// old one-shot listener, editor allowlist, machine pins or default preflight.
internal static class HotkeyPattern
{
    internal sealed record Target(int Pid, nint Hwnd, DateTime CreatedUtc, string ImagePath);
    internal static bool IsAltCandidate(uint vk) => vk is 0x12 or 0xA4 or 0xA5;
    internal static bool MatchesForwardedRightAlt(uint vk, uint scan, uint flags, nuint extra) =>
        RightAltRejectionReason(vk, scan, flags, extra) is null;

    internal static string? RightAltRejectionReason(uint vk, uint scan, uint flags, nuint extra)
    {
        if (vk != 0xA5) return "not-right-alt-vk";
        if (scan is not (0x38 or 0x138)) return "scan-not-38-or-138";
        if ((flags & 0x10) == 0) return "physical-or-non-injected-pass-through";
        if ((flags & 1) == 0) return "not-extended";
        if ((flags & 2) != 0) return "lower-integrity-pattern-pass-through";
        if (extra != 0) return "extra-tag-pattern-pass-through";
        return null;
    }

    internal static bool MatchesPreparedIdentity(int pid, DateTime createdUtc, string imagePath, ProcessIdentityResult actual) =>
        actual.Status == "read-only-identity-ok" && actual.Pid == pid && actual.CreatedUtc == createdUtc &&
        string.Equals(imagePath, actual.ImagePath, StringComparison.OrdinalIgnoreCase);
}
