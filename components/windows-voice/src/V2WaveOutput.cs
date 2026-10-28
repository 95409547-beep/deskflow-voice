// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace DeskflowVoice.Windows;

// V2 strict drain adapter. The existing Receiver.cs/legacy protocol is unchanged.
internal sealed class V2WaveOutput : IDisposable
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct Caps
    {
        public ushort Manufacturer, Product; public uint Version;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Name;
        public uint Formats; public ushort Channels, Reserved; public uint Support;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 2)] private struct Format
    { public ushort Tag, Channels; public uint Samples, Bytes; public ushort Align, Bits, Extra; }
    [StructLayout(LayoutKind.Sequential)] private struct Header
    { public nint Data; public uint Length, Recorded; public nuint User; public uint Flags, Loops; public nint Next; public nuint Reserved; }
    private sealed record Buffer(nint Data, nint Header, int Bytes);
    private readonly List<Buffer> pending = new();
    private readonly uint headerSize = (uint)Marshal.SizeOf<Header>();
    private nint handle; private int queued;
    [DllImport("winmm.dll")] private static extern uint waveOutGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] private static extern uint waveOutGetDevCapsW(nuint id, out Caps caps, uint size);
    [DllImport("winmm.dll")] private static extern uint waveOutOpen(out nint handle, uint id, ref Format format, nint callback, nint instance, uint flags);
    [DllImport("winmm.dll")] private static extern uint waveOutPrepareHeader(nint handle, nint header, uint size);
    [DllImport("winmm.dll")] private static extern uint waveOutWrite(nint handle, nint header, uint size);
    [DllImport("winmm.dll")] private static extern uint waveOutUnprepareHeader(nint handle, nint header, uint size);
    [DllImport("winmm.dll")] private static extern uint waveOutReset(nint handle);
    [DllImport("winmm.dll")] private static extern uint waveOutClose(nint handle);
    private static void Check(uint code) { if (code != 0) throw new IOException("wave_output_error_" + code); }
    internal static uint ResolveUniqueDevice(string prefix)
    {
        var ids = new List<uint>();
        for (uint id = 0; id < waveOutGetNumDevs(); id++)
        {
            Check(waveOutGetDevCapsW(id, out var caps, (uint)Marshal.SizeOf<Caps>()));
            if (caps.Name.StartsWith(prefix, StringComparison.Ordinal)) ids.Add(id);
        }
        if (ids.Count != 1) throw new InvalidOperationException("Output prefix must resolve to exactly one device");
        return ids[0];
    }
    internal V2WaveOutput(string prefix)
    {
        var id = ResolveUniqueDevice(prefix);
        var format = new Format { Tag = 1, Channels = 2, Samples = 48000, Bytes = 192000, Align = 4, Bits = 16 };
        Check(waveOutOpen(out handle, id, ref format, 0, 0, 0));
    }
    private void Reap()
    {
        for (var i = pending.Count - 1; i >= 0; i--)
        {
            var buffer = pending[i];
            if ((Marshal.PtrToStructure<Header>(buffer.Header).Flags & 1) == 0) continue;
            Check(waveOutUnprepareHeader(handle, buffer.Header, headerSize));
            Marshal.FreeHGlobal(buffer.Header); Marshal.FreeHGlobal(buffer.Data); queued -= buffer.Bytes; pending.RemoveAt(i);
        }
    }
    internal void Write(byte[] mono)
    {
        var stereo = new byte[mono.Length * 2]; var watch = Stopwatch.StartNew();
        WaveQueueBackpressure.WaitForCapacity(stereo.Length, () => { Reap(); return queued; }, () => watch.ElapsedMilliseconds, Thread.Sleep);
        for (int i = 0, j = 0; i < mono.Length; i += 2, j += 4) { stereo[j] = stereo[j + 2] = mono[i]; stereo[j + 1] = stereo[j + 3] = mono[i + 1]; }
        var buffer = new Buffer(Marshal.AllocHGlobal(stereo.Length), Marshal.AllocHGlobal((int)headerSize), stereo.Length);
        var prepared = false;
        try
        {
            Marshal.Copy(stereo, 0, buffer.Data, stereo.Length);
            Marshal.StructureToPtr(new Header { Data = buffer.Data, Length = (uint)stereo.Length }, buffer.Header, false);
            Check(waveOutPrepareHeader(handle, buffer.Header, headerSize)); prepared = true;
            Check(waveOutWrite(handle, buffer.Header, headerSize)); pending.Add(buffer); queued += buffer.Bytes;
        }
        catch { if (prepared) waveOutUnprepareHeader(handle, buffer.Header, headerSize); Marshal.FreeHGlobal(buffer.Header); Marshal.FreeHGlobal(buffer.Data); throw; }
    }
    internal void Drain()
    {
        var watch = Stopwatch.StartNew();
        while (pending.Count > 0 && watch.ElapsedMilliseconds < 500) { Reap(); Thread.Sleep(5); }
        if (pending.Count != 0) throw new IOException("audio_drain_timeout");
    }
    internal void Reset() { if (handle != 0) Check(waveOutReset(handle)); }
    public void Dispose()
    {
        if (handle == 0) return;
        if (waveOutReset(handle) == 0)
        {
            foreach (var buffer in pending) if (waveOutUnprepareHeader(handle, buffer.Header, headerSize) == 0)
            { Marshal.FreeHGlobal(buffer.Header); Marshal.FreeHGlobal(buffer.Data); }
        }
        pending.Clear(); queued = 0; waveOutClose(handle); handle = 0;
    }
}
