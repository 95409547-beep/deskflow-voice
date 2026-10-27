// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace DeskflowVoice.Windows;

internal sealed record ProcessIdentityResult(int Pid, string? ImagePath, DateTime? CreatedUtc, int? Win32Error, string Status);

internal static class ProcessIdentity
{
    internal static ProcessIdentityResult TryRead(int pid)
    {
        using var handle = Os.OpenProcess(0x1000, false, pid);
        if (handle.IsInvalid) return new(pid, null, null, Marshal.GetLastWin32Error(), "read-denied-or-unavailable");
        var text = new StringBuilder(32768); var size = text.Capacity;
        if (!Os.QueryFullProcessImageNameW(handle, 0, text, ref size)) return new(pid, null, null, Marshal.GetLastWin32Error(), "image-query-failed");
        if (!Os.GetProcessTimes(handle, out var created, out _, out _, out _)) return new(pid, text.ToString(), null, Marshal.GetLastWin32Error(), "time-query-failed");
        return new(pid, text.ToString(), DateTime.FromFileTimeUtc(created), null, "read-only-identity-ok");
    }
}

internal static class RpcAbiAssumption
{
    internal const string PipePath = @"\\.\pipe\ObricIme\oime-server";
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal delegate void FocusIn([MarshalAs(UnmanagedType.LPStr)] string pipe);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal delegate int SimpleMessage([MarshalAs(UnmanagedType.LPStr)] string pipe, int message, int first, int second);
}

internal sealed class RpcLibrary : IDisposable
{
    private readonly nint module;
    internal string LoadedModulePath { get; }
    internal RpcAbiAssumption.FocusIn Focus { get; }
    internal RpcAbiAssumption.SimpleMessage Message { get; }
    private RpcLibrary(nint handle, string path)
    {
        module = handle; LoadedModulePath = path;
        Focus = Marshal.GetDelegateForFunctionPointer<RpcAbiAssumption.FocusIn>(Address("RpcPipe_FocusIn"));
        Message = Marshal.GetDelegateForFunctionPointer<RpcAbiAssumption.SimpleMessage>(Address("RpcPipe_SimpleMessage"));
    }
    internal static RpcLibrary LoadPinned(string verifiedPath)
    {
        var path = Path.GetFullPath(verifiedPath);
        var handle = Os.LoadLibraryExW(path, 0, 0x100 | 0x800);
        if (handle == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Verified RPC module load failed");
        try
        {
            var actual = new StringBuilder(32768);
            var size = Os.GetModuleFileNameW(handle, actual, (uint)actual.Capacity);
            if (size == 0 || size >= actual.Capacity || !string.Equals(Path.GetFullPath(actual.ToString()), path, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Loaded module path mismatch");
            return new RpcLibrary(handle, path);
        }
        catch { Os.FreeLibrary(handle); throw; }
    }
    private nint Address(string symbol)
    {
        var result = Os.GetProcAddress(module, symbol);
        return result != 0 ? result : throw new Win32Exception(Marshal.GetLastWin32Error(), "Missing compatibility export: " + symbol);
    }
    public void Dispose() => Os.FreeLibrary(module);
}

// Authored interop declarations for Windows APIs; no SDK/vendor source or binary is bundled.
internal static class Os
{
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern SafeProcessHandle OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] internal static extern bool QueryFullProcessImageNameW(SafeProcessHandle process, uint flags, StringBuilder text, ref int size);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetProcessTimes(SafeProcessHandle process, out long created, out long exited, out long kernel, out long user);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] internal static extern SafeFileHandle CreateFileW(string path, uint access, uint share, nint attributes, uint disposition, uint flags, nint template);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetNamedPipeServerProcessId(SafeFileHandle pipe, out uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] internal static extern nint LoadLibraryExW(string file, nint reserved, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] internal static extern uint GetModuleFileNameW(nint module, StringBuilder name, uint size);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true, ExactSpelling = true)] internal static extern nint GetProcAddress(nint module, string symbol);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool FreeLibrary(nint module);
    [DllImport("user32.dll")] internal static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] internal static extern uint GetWindowThreadProcessId(nint window, out uint pid);
}
