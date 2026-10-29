// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal static class ManagedLifetime
{
    // The explicit managed entry owns redirected stdin. EOF means its GUI is gone.
    internal static async Task Watch(TextReader input, CancellationTokenSource lifetime)
    {
        try
        {
            while (!lifetime.IsCancellationRequested)
            {
                var line = await input.ReadLineAsync(lifetime.Token).ConfigureAwait(false);
                if (line is null || line == "stop") { lifetime.Cancel(); return; }
                // Unknown commands cannot trigger a native action.
            }
        }
        catch (Exception error) when (error is OperationCanceledException or ObjectDisposedException or IOException)
        {
            try { lifetime.Cancel(); } catch (ObjectDisposedException) { }
        }
    }
}

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal static class ManagedReceiver
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Blob { internal int Length; internal IntPtr Data; }
    [DllImport("crypt32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(ref Blob input, IntPtr description, IntPtr entropy,
        IntPtr reserved, IntPtr prompt, uint flags, out Blob output);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    private static string ReadSecret(string path)
    {
        var encrypted = File.ReadAllBytes(path);
        if (encrypted.Length is 0 or > 65536) throw new InvalidDataException("Invalid protected secret");
        var pinned = GCHandle.Alloc(encrypted, GCHandleType.Pinned);
        var input = new Blob { Length = encrypted.Length, Data = pinned.AddrOfPinnedObject() };
        Blob output = default;
        try
        {
            if (!CryptUnprotectData(ref input, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 1, out output))
                throw new CryptographicException("Cannot read this user's protected pairing secret");
            if (output.Length is <= 0 or > 4096) throw new InvalidDataException("Invalid secret size");
            var bytes = new byte[output.Length];
            try { Marshal.Copy(output.Data, bytes, 0, bytes.Length); return Encoding.UTF8.GetString(bytes); }
            finally { CryptographicOperations.ZeroMemory(bytes); }
        }
        finally
        {
            if (output.Data != IntPtr.Zero)
            {
                for (int index = 0; index < output.Length; ++index) Marshal.WriteByte(output.Data, index, 0);
                LocalFree(output.Data);
            }
            pinned.Free();
        }
    }
    internal static int Run(string config)
    {
        if (!Console.IsInputRedirected) throw new InvalidOperationException("Managed receiver requires a GUI-owned input pipe");
        var absolute = Path.GetFullPath(config);
        using var document = JsonDocument.Parse(File.ReadAllText(absolute));
        var root = document.RootElement;
        var directory = Path.GetDirectoryName(absolute)!;
        var tokenPath = root.GetProperty("TokenDpapiPath").GetString() ?? throw new InvalidDataException("No protected token");
        var tokenName = root.GetProperty("TokenEnvironment").GetString() ?? throw new InvalidDataException("No token binding");
        if (tokenName != "DESKFLOW_VOICE_V2_TOKEN") throw new InvalidDataException("Unexpected token binding");
        string? passwordName = null;
        V2Options options;
        try
        {
            var token = ReadSecret(Configuration.ResolvePath(tokenPath, directory));
            if (!Configuration.IsSha256(token)) throw new InvalidDataException("Invalid protected token");
            Environment.SetEnvironmentVariable(tokenName, token);
            if (root.TryGetProperty("CertificatePasswordDpapiPath", out var protectedPassword))
            {
                passwordName = root.GetProperty("CertificatePasswordEnvironment").GetString();
                if (passwordName != "DESKFLOW_VOICE_V2_CERT_PASSWORD") throw new InvalidDataException("Unexpected password binding");
                Environment.SetEnvironmentVariable(passwordName, ReadSecret(Configuration.ResolvePath(protectedPassword.GetString()!, directory)));
            }
            options = V2Options.Load(absolute);
        }
        finally
        {
            Environment.SetEnvironmentVariable(tokenName, null);
            if (passwordName is not null) Environment.SetEnvironmentVariable(passwordName, null);
        }
        return V2Server.Run(options, true);
    }
}
