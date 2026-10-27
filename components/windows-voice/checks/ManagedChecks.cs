// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text;
using System.Text.Json;

namespace DeskflowVoice.Windows;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal static class ManagedChecks
{
    internal static object Run()
    {
        var checks = RepeatChecks.Run();
        AddJson(checks, "receiver", ForegroundReceiverChecks.Run());
        AddJson(checks, "queue", WaveQueueChecks.Run());
        AddCore(checks);
        if (checks.Values.Any(passed => !passed)) throw new InvalidOperationException("A managed assertion failed");
        return new { Passed = checks.Count, Checks = checks, NativeRpcLoaded = false, HookInstalled = false,
            KeysSent = 0, AudioDeviceOpened = false, ListenerStarted = false, RuntimeConfigurationRead = false };
    }
    private static void AddJson(Dictionary<string, bool> checks, string prefix, string json)
    {
        using var document = JsonDocument.Parse(json);
        foreach (var item in document.RootElement.GetProperty("checks").EnumerateObject()) checks.Add(prefix + ":" + item.Name, item.Value.GetBoolean());
    }
    private static bool Rejects(Action action)
    {
        try { action(); return false; } catch (Exception error) when (error is InvalidDataException or ArgumentException or EndOfStreamException or JsonException) { return true; }
    }
    private static void AddCore(Dictionary<string, bool> checks)
    {
        checks["cli:no-args-help"] = CommandLine.Parse([]) == Command.Help;
        checks["cli:explicit-help"] = CommandLine.Parse(["--help"]) == Command.Help;
        checks["cli:managed-only-entry"] = CommandLine.Parse(["--check-managed"]) == Command.ManagedChecks;
        checks["cli:old-live-mode-rejected"] = CommandLine.Parse(["--right-alt-repeat"]) == Command.Invalid;
        checks["cli:extra-args-rejected"] = CommandLine.Parse(["--check-managed", "--enable"]) == Command.Invalid;
        checks["pattern:forwarded-down"] = HotkeyPattern.MatchesForwardedRightAlt(0xA5, 0x38, 0x31, 0);
        checks["pattern:forwarded-up"] = HotkeyPattern.MatchesForwardedRightAlt(0xA5, 0x138, 0x91, 0);
        checks["pattern:physical-passes"] = !HotkeyPattern.MatchesForwardedRightAlt(0xA5, 0x38, 0x21, 0);
        checks["pattern:other-origin-tag-passes"] = !HotkeyPattern.MatchesForwardedRightAlt(0xA5, 0x38, 0x31, 1);
        checks["pattern:lower-integrity-passes"] = !HotkeyPattern.MatchesForwardedRightAlt(0xA5, 0x38, 0x33, 0);
        checks["config:hash-shape"] = Configuration.IsSha256(new string('a', 64)) && !Configuration.IsSha256("invalid");
        checks["config:missing-native-pins-fails-before-probe"] = Rejects(() => new InputMethodOptions().Validate());
        var root = Path.Combine(Path.GetTempPath(), "FixtureVendor");
        checks["config:installation-child"] = CurrentVoiceService.IsUnderRoot(root, Path.Combine(root, "versions", "current", "ImeService.exe"));
        checks["config:installation-sibling-rejected"] = !CurrentVoiceService.IsUnderRoot(root, Path.Combine(root + "Sibling", "ImeService.exe"));
        var settings = new Settings { bind_address = "127.0.0.1", allowed_peer = "127.0.0.1", port = 24812,
            token = new string('a', 64), certificate_path = Path.Combine(Path.GetTempPath(), "fixture-certificate.pfx"),
            waveout_name_prefix = "Fixture Output", hotkey_verified = true, hotkey = ["RALT"] };
        settings.ValidateForRun(); checks["config:explicit-settings-validate-without-opening-devices"] = true;
        settings.waveout_name_prefix = "";
        checks["config:no-implicit-audio-endpoint"] = Rejects(settings.ValidateForRun);
        checks["auth:constant-time-match"] = Framing.TokenEquals(new string('a', 64), new string('a', 64));
        checks["auth:mismatch-rejected"] = !Framing.TokenEquals(new string('a', 64), new string('b', 64));
        using var framed = new MemoryStream();
        Framing.Write(framed, new { type = "ping" }, CancellationToken.None).GetAwaiter().GetResult(); framed.Position = 0;
        using var message = Framing.Read(framed, CancellationToken.None).GetAwaiter().GetResult();
        checks["frame:managed-roundtrip"] = Framing.Type(message.RootElement) == "ping";
        checks["frame:zero-size-rejected"] = Rejects(() => Framing.Read(new MemoryStream(new byte[4]), CancellationToken.None).GetAwaiter().GetResult());
        checks["frame:oversize-rejected"] = Rejects(() => Framing.Read(new MemoryStream(new byte[] { 0, 1, 0, 1 }), CancellationToken.None).GetAwaiter().GetResult());
        checks["frame:truncated-rejected"] = Rejects(() => Framing.Read(new MemoryStream(new byte[] { 0, 0, 0, 3, 123 }), CancellationToken.None).GetAwaiter().GetResult());
        checks["frame:type-required"] = Rejects(() => { using var value = JsonDocument.Parse("{}"); Framing.Type(value.RootElement); });
        checks["frame:session-uuid-required"] = Rejects(() => { using var value = JsonDocument.Parse("{\"session\":\"not-a-uuid\"}"); Framing.Session(value.RootElement); });
    }
}
