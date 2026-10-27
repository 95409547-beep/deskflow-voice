// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Net;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace DeskflowVoice.Windows;

public sealed partial class Settings
{
    public string token_environment { get; set; } = "DESKFLOW_VOICE_TOKEN";
    public string? certificate_password_environment { get; set; }

    public void ValidateForRun()
    {
        if (!IPAddress.TryParse(bind_address, out _) || !IPAddress.TryParse(allowed_peer, out _))
            throw new InvalidDataException("Explicit bind and allowed-peer IP addresses are required");
        if (port is < 1 or > 65535) throw new InvalidDataException("Invalid port");
        if (!Configuration.IsSha256(token)) throw new InvalidDataException("Expected a 64-character pairing token from the configured environment variable");
        if (!Path.IsPathFullyQualified(certificate_path)) throw new InvalidDataException("Certificate path must resolve to an absolute path");
        if (string.IsNullOrWhiteSpace(waveout_name_prefix)) throw new InvalidDataException("An explicit output-device prefix is required");
        if (!hotkey_verified || hotkey is null || hotkey.Length != 1 || hotkey[0] != "RALT")
            throw new InvalidDataException("The foreground compatibility adapter currently requires a verified RALT gesture");
    }
}

internal sealed class InputMethodOptions
{
    public string? InstallationRoot { get; set; }
    public string ServiceSha256 { get; set; } = "";
    public string RpcSha256 { get; set; } = "";
    public string SignerThumbprint { get; set; } = "";

    internal string ResolveInstallationRoot() => Path.GetFullPath(Environment.ExpandEnvironmentVariables(
        InstallationRoot ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "DoubaoIME")));

    internal void Validate()
    {
        if (!Configuration.IsSha256(ServiceSha256) || !Configuration.IsSha256(RpcSha256) ||
            !Regex.IsMatch(SignerThumbprint, "\\A[0-9a-fA-F]{40}\\z"))
            throw new InvalidDataException("Native compatibility requires explicit file hashes and signer thumbprint from installation verification");
    }
}

internal sealed class ControllerOptions
{
    public string? StateDirectory { get; set; }
    public InputMethodOptions InputMethod { get; set; } = new();
    internal string ResolveStateDirectory() => Path.GetFullPath(Environment.ExpandEnvironmentVariables(
        StateDirectory ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskflowVoice", "Windows")));
}

internal static class Configuration
{
    internal static bool IsSha256(string? value) => value is not null && Regex.IsMatch(value, "\\A[0-9a-fA-F]{64}\\z");

    internal static string ResolvePath(string value, string configDirectory) => Path.GetFullPath(
        Environment.ExpandEnvironmentVariables(value), configDirectory);

    public static Settings LoadReceiver(string configPath)
    {
        var path = Path.GetFullPath(configPath);
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        if (document.RootElement.TryGetProperty("token", out _) || document.RootElement.TryGetProperty("certificate_password", out _))
            throw new InvalidDataException("Use environment references instead of embedding secrets in the settings document");
        var settings = document.Deserialize<Settings>() ?? throw new InvalidDataException("Empty receiver settings");
        settings.token = Environment.GetEnvironmentVariable(settings.token_environment) ?? "";
        settings.certificate_password = settings.certificate_password_environment is null ? null :
            Environment.GetEnvironmentVariable(settings.certificate_password_environment);
        settings.certificate_path = ResolvePath(settings.certificate_path, Path.GetDirectoryName(path)!);
        settings.ValidateForRun();
        return settings;
    }
}
