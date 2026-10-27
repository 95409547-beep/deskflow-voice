// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Microsoft.Win32;

namespace DeskflowVoice.Windows;

internal sealed record VoiceServicePin(int Pid, string ImagePath, DateTime CreatedUtc);
internal sealed record MicrophoneUsage(long LastStart, long LastStop)
{
    internal bool Ended => LastStart > 0 && LastStop >= LastStart;
}

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
internal sealed class CurrentVoiceService
{
    internal VoiceServicePin Pin { get; }
    internal string RpcPath { get; }
    private CurrentVoiceService(VoiceServicePin pin) { Pin = pin; RpcPath = Path.Combine(Path.GetDirectoryName(pin.ImagePath)!, "rpc.dll"); }

    internal static VoiceServicePin BindUnique(IEnumerable<ProcessIdentityResult> candidates, string expectedImagePath)
    {
        var all = candidates.ToArray();
        if (all.Any(p => p.Pid <= 0 || p.Status != "read-only-identity-ok" || p.CreatedUtc is null || p.ImagePath is null))
            throw new InvalidOperationException("Service identity unreadable; no fallback");
        var matches = all.Where(p => string.Equals(p.ImagePath, expectedImagePath, StringComparison.OrdinalIgnoreCase)).ToArray();
        if (matches.Length != 1) throw new InvalidOperationException("Need exactly one installed service identity");
        return new(matches[0].Pid, matches[0].ImagePath!, matches[0].CreatedUtc!.Value);
    }
    internal static void RequireSame(VoiceServicePin pin, ProcessIdentityResult actual)
    {
        if (actual.Status != "read-only-identity-ok" || actual.Pid != pin.Pid || actual.CreatedUtc != pin.CreatedUtc ||
            !string.Equals(actual.ImagePath, pin.ImagePath, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Service changed; no in-session rebinding");
    }
    internal static CurrentVoiceService Prepare(InputMethodOptions options)
    {
        options.Validate();
        var root = options.ResolveInstallationRoot();
        var candidates = new List<ProcessIdentityResult>();
        foreach (var process in Process.GetProcessesByName("ImeService"))
        {
            using (process) candidates.Add(ProcessIdentity.TryRead(process.Id));
        }
        if (candidates.Any(p => p.ImagePath is null || p.CreatedUtc is null)) throw new InvalidOperationException("Unreadable service identity");
        var installed = candidates.Where(p => IsUnderRoot(root, p.ImagePath!)).ToArray();
        if (installed.Length != 1) throw new InvalidOperationException("Need one service under the configured installation root");
        var service = new CurrentVoiceService(BindUnique(installed, installed[0].ImagePath!));
        VerifyFile(service.Pin.ImagePath, options.ServiceSha256, options.SignerThumbprint);
        VerifyFile(service.RpcPath, options.RpcSha256, options.SignerThumbprint);
        service.ValidateIdleAndPipe();
        return service;
    }
    internal static bool IsUnderRoot(string root, string file)
    {
        var relative = Path.GetRelativePath(Path.GetFullPath(root), Path.GetFullPath(file));
        return !Path.IsPathRooted(relative) && relative != ".." && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
    }
    private static void VerifyFile(string path, string hash, string signer)
    {
        using var stream = File.OpenRead(path);
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(stream)), hash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Installed file hash mismatch");
        Authenticode.RequireValid(path, signer);
    }
    internal MicrophoneUsage ReadMicrophoneUsage()
    {
        var encodedPath = Pin.ImagePath.Replace('\\', '#');
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\NonPackaged\" + encodedPath, false);
        return new(Convert.ToInt64(key?.GetValue("LastUsedTimeStart") ?? 0), Convert.ToInt64(key?.GetValue("LastUsedTimeStop") ?? 0));
    }
    internal void ValidateIdentity() => RequireSame(Pin, ProcessIdentity.TryRead(Pin.Pid));
    internal void ValidateIdleAndPipe()
    {
        ValidateIdentity();
        if (!ReadMicrophoneUsage().Ended) throw new InvalidOperationException("Microphone not confirmed ended");
        using var pipe = Os.CreateFileW(RpcAbiAssumption.PipePath, 0x80, 3, 0, 3, 0, 0);
        if (pipe.IsInvalid || !Os.GetNamedPipeServerProcessId(pipe, out var pid) || pid != Pin.Pid)
            throw new InvalidOperationException("Pipe owner does not match this service binding");
    }
}

internal static class Authenticode
{
    [StructLayout(LayoutKind.Sequential)] private struct FileInfo { public uint Size; public nint Path, Handle, Subject; }
    [StructLayout(LayoutKind.Sequential)] private struct TrustData
    {
        public uint Size; public nint Policy, Sip; public uint Ui, Revocation, Choice;
        public nint File; public uint StateAction; public nint State, Url; public uint Flags, Context; public nint Signature;
    }
    [DllImport("wintrust.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern int WinVerifyTrust(nint window, ref Guid action, ref TrustData data);
    [DllImport("wintrust.dll", ExactSpelling = true)] private static extern nint WTHelperProvDataFromStateData(nint state);
    [DllImport("wintrust.dll", ExactSpelling = true)] private static extern nint WTHelperGetProvSignerFromChain(nint provider, uint signer, [MarshalAs(UnmanagedType.Bool)] bool counterSigner, uint counterIndex);
    [DllImport("wintrust.dll", ExactSpelling = true)] private static extern nint WTHelperGetProvCertFromChain(nint signer, uint index);
    [StructLayout(LayoutKind.Sequential)] private struct ProviderCertificate { public uint Size; public nint Certificate; }
    [StructLayout(LayoutKind.Sequential)] private struct CertificateContext { public uint Encoding; public nint Encoded; public uint Length; public nint Info, Store; }

    internal static void RequireValid(string path, string expectedSigner)
    {
        var name = Marshal.StringToCoTaskMemUni(path);
        var file = Marshal.AllocHGlobal(Marshal.SizeOf<FileInfo>());
        var data = new TrustData { Size = (uint)Marshal.SizeOf<TrustData>(), Ui = 2, Choice = 1, File = file, StateAction = 1, Flags = 0x1000 };
        var action = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");
        try
        {
            Marshal.StructureToPtr(new FileInfo { Size = (uint)Marshal.SizeOf<FileInfo>(), Path = name }, file, false);
            var result = WinVerifyTrust(-1, ref action, ref data);
            if (result != 0) throw new InvalidOperationException("Authenticode validation failed: 0x" + result.ToString("X8"));
            var provider = WTHelperProvDataFromStateData(data.State);
            var signer = provider == 0 ? 0 : WTHelperGetProvSignerFromChain(provider, 0, false, 0);
            var entry = signer == 0 ? 0 : WTHelperGetProvCertFromChain(signer, 0);
            if (entry == 0) throw new InvalidOperationException("Verified signer certificate unavailable");
            var certificatePointer = Marshal.PtrToStructure<ProviderCertificate>(entry).Certificate;
            if (certificatePointer == 0) throw new InvalidOperationException("Verified certificate context unavailable");
            var context = Marshal.PtrToStructure<CertificateContext>(certificatePointer);
            if (context.Encoded == 0 || context.Length is 0 or > 1048576) throw new InvalidOperationException("Invalid signer certificate length");
            var der = new byte[context.Length]; Marshal.Copy(context.Encoded, der, 0, der.Length);
            using var certificate = X509CertificateLoader.LoadCertificate(der);
            if (!string.Equals(certificate.Thumbprint, expectedSigner, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Installed signer mismatch");
        }
        finally
        {
            if (data.State != 0) { data.StateAction = 2; WinVerifyTrust(-1, ref action, ref data); }
            Marshal.FreeHGlobal(file); Marshal.FreeCoTaskMem(name);
        }
    }
}
