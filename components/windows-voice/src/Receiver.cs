// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#nullable disable
using System;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace DeskflowVoice.Windows {
  public sealed partial class Settings {
    public string bind_address { get; set; }
    public int port { get; set; }
    public string allowed_peer { get; set; }
    [System.Text.Json.Serialization.JsonIgnore] public string token { get; set; }
    public string certificate_path { get; set; }
    [System.Text.Json.Serialization.JsonIgnore] public string certificate_password { get; set; }
    public string certificate_sha256 { get; set; }
    public string waveout_name_prefix { get; set; }
    public bool hotkey_verified { get; set; }
    public string[] hotkey { get; set; }
  }
  public static class Framing {
    public const int MaxFrame=65536;
    static async Task Exact(Stream stream,byte[] buffer,CancellationToken ct) {
      int offset=0;
      while(offset<buffer.Length) {
        int count=await stream.ReadAsync(buffer,offset,buffer.Length-offset,ct).ConfigureAwait(false);
        if(count==0) throw new EndOfStreamException();
        offset+=count;
      }
    }
    public static async Task<JsonDocument> Read(Stream stream,CancellationToken ct) {
      byte[] header=new byte[4]; await Exact(stream,header,ct).ConfigureAwait(false);
      uint size=((uint)header[0]<<24)|((uint)header[1]<<16)|((uint)header[2]<<8)|header[3];
      if(size==0||size>MaxFrame) throw new InvalidDataException("frame_length");
      byte[] bytes=new byte[size]; await Exact(stream,bytes,ct).ConfigureAwait(false);
      return JsonDocument.Parse(bytes,new JsonDocumentOptions{MaxDepth=8});
    }
    public static async Task Write(Stream stream,object value,CancellationToken ct) {
      byte[] bytes=JsonSerializer.SerializeToUtf8Bytes(value);
      if(bytes.Length>MaxFrame) throw new InvalidDataException("response_length");
      byte[] header={(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length};
      await stream.WriteAsync(header,0,4,ct).ConfigureAwait(false);
      await stream.WriteAsync(bytes,0,bytes.Length,ct).ConfigureAwait(false);
      await stream.FlushAsync(ct).ConfigureAwait(false);
    }
    public static string Type(JsonElement e) {
      JsonElement p; if(!e.TryGetProperty("type",out p)||p.ValueKind!=JsonValueKind.String) throw new InvalidDataException("missing_type");
      return p.GetString();
    }
    public static string Session(JsonElement e) {
      JsonElement p; Guid value;
      if(!e.TryGetProperty("session",out p)||p.ValueKind!=JsonValueKind.String||!Guid.TryParse(p.GetString(),out value)) throw new InvalidDataException("invalid_session");
      return value.ToString("D");
    }
    public static bool TokenEquals(string a,string b) {
      if(a==null||b==null) return false;
      byte[] aa=Encoding.UTF8.GetBytes(a), bb=Encoding.UTF8.GetBytes(b);
      return aa.Length==bb.Length && CryptographicOperations.FixedTimeEquals(aa,bb);
    }
  }
  public static class WaveQueueBackpressure {
    // 48000 frames/s * 2 channels * 2 bytes/sample = 192000 bytes/s.
    // Count whole stereo buffers until WHDR_DONE permits their reclamation.
    public const int CapacityBytes=48000; // 250ms, unchanged
    public const int WaitTimeoutMs=500;
    public static void WaitForCapacity(int nextBytes,Func<int> reapQueuedBytes,Func<long> clockMs,Action<int> waitMs) {
      if(nextBytes<=0||nextBytes>CapacityBytes) throw new IOException("audio_frame_exceeds_250ms_capacity");
      long started=clockMs();
      while(true) {
        int queued=reapQueuedBytes();
        long elapsed=clockMs()-started;
        if(elapsed>=WaitTimeoutMs) throw new IOException("audio_output_backpressure_timeout_500ms");
        if(queued+nextBytes<=CapacityBytes)return;
        waitMs((int)Math.Min(5,WaitTimeoutMs-elapsed));
      }
    }
  }
  public sealed class WaveSink : IDisposable {
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Caps {
      public ushort manufacturer,product; public uint version;
      [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string name;
      public uint formats; public ushort channels,reserved; public uint support;
    }
    [StructLayout(LayoutKind.Sequential,Pack=2)] struct Format {
      public ushort tag,channels; public uint samples,bytes; public ushort align,bits,extra;
    }
    [StructLayout(LayoutKind.Sequential)] struct Header {
      public IntPtr data; public uint length,recorded; public UIntPtr user;
      public uint flags,loops; public IntPtr next; public UIntPtr reserved;
    }
    sealed class Buffer { public IntPtr data,header; public int bytes; }
    [DllImport("winmm.dll")] static extern uint waveOutGetNumDevs();
    [DllImport("winmm.dll",CharSet=CharSet.Unicode)] static extern uint waveOutGetDevCapsW(UIntPtr id,out Caps caps,uint size);
    [DllImport("winmm.dll")] static extern uint waveOutOpen(out IntPtr handle,uint id,ref Format format,IntPtr callback,IntPtr instance,uint flags);
    [DllImport("winmm.dll")] static extern uint waveOutPrepareHeader(IntPtr handle,IntPtr header,uint size);
    [DllImport("winmm.dll")] static extern uint waveOutWrite(IntPtr handle,IntPtr header,uint size);
    [DllImport("winmm.dll")] static extern uint waveOutUnprepareHeader(IntPtr handle,IntPtr header,uint size);
    [DllImport("winmm.dll")] static extern uint waveOutReset(IntPtr handle);
    [DllImport("winmm.dll")] static extern uint waveOutClose(IntPtr handle);
    IntPtr handle; readonly List<Buffer> pending=new List<Buffer>(); int queuedBytes;
    readonly uint headerSize=(uint)Marshal.SizeOf(typeof(Header));
    static void Check(uint rc,string api) { if(rc!=0) throw new IOException(api+":"+rc); }
    public WaveSink(string prefix) {
      if(string.IsNullOrWhiteSpace(prefix)) throw new InvalidOperationException("An explicit output device prefix is required");
      var ids=new List<uint>();
      for(uint i=0;i<waveOutGetNumDevs();i++) { Caps c; Check(waveOutGetDevCapsW((UIntPtr)i,out c,(uint)Marshal.SizeOf(typeof(Caps))),"waveOutGetDevCaps"); if(c.name.StartsWith(prefix,StringComparison.Ordinal)) ids.Add(i); }
      if(ids.Count!=1) throw new InvalidOperationException("CABLE Input endpoint must resolve uniquely");
      Format f=new Format{tag=1,channels=2,samples=48000,bytes=192000,align=4,bits=16,extra=0};
      Check(waveOutOpen(out handle,ids[0],ref f,IntPtr.Zero,IntPtr.Zero,0),"waveOutOpen");
    }
    public void Reap() {
      for(int i=pending.Count-1;i>=0;i--) {
        var b=pending[i]; var h=Marshal.PtrToStructure<Header>(b.header);
        if((h.flags&1)==0) continue;
        Check(waveOutUnprepareHeader(handle,b.header,headerSize),"waveOutUnprepareHeader");
        Marshal.FreeHGlobal(b.header); Marshal.FreeHGlobal(b.data); queuedBytes-=b.bytes; pending.RemoveAt(i);
      }
    }
    public void Write(byte[] mono) {
      byte[] stereo=new byte[mono.Length*2];
      var wait=Stopwatch.StartNew();
      // Bound the wait rather than rejecting a short startup/network burst.
      // IOException still propagates to the unchanged receiver Finish cleanup.
      WaveQueueBackpressure.WaitForCapacity(stereo.Length,()=>{Reap();return queuedBytes;},()=>wait.ElapsedMilliseconds,Thread.Sleep);
      for(int i=0,j=0;i<mono.Length;i+=2,j+=4) { stereo[j]=stereo[j+2]=mono[i]; stereo[j+1]=stereo[j+3]=mono[i+1]; }
      var b=new Buffer{data=Marshal.AllocHGlobal(stereo.Length),header=Marshal.AllocHGlobal((int)headerSize),bytes=stereo.Length};
      bool prepared=false;
      try {
        Marshal.Copy(stereo,0,b.data,stereo.Length);
        var h=new Header{data=b.data,length=(uint)stereo.Length}; Marshal.StructureToPtr(h,b.header,false);
        Check(waveOutPrepareHeader(handle,b.header,headerSize),"waveOutPrepareHeader"); prepared=true;
        Check(waveOutWrite(handle,b.header,headerSize),"waveOutWrite");
        pending.Add(b); queuedBytes+=b.bytes;
      } catch { if(prepared) waveOutUnprepareHeader(handle,b.header,headerSize); Marshal.FreeHGlobal(b.header); Marshal.FreeHGlobal(b.data); throw; }
    }
    public void Drain() { var end=Stopwatch.StartNew(); while(pending.Count>0&&end.ElapsedMilliseconds<300) { Reap(); Thread.Sleep(5); } }
    public void Dispose() {
      if(handle==IntPtr.Zero) return;
      uint reset=waveOutReset(handle);
      if(reset==0) {
        foreach(var b in pending) { if(waveOutUnprepareHeader(handle,b.header,headerSize)==0) { Marshal.FreeHGlobal(b.header); Marshal.FreeHGlobal(b.data); } }
      }
      pending.Clear(); queuedBytes=0; waveOutClose(handle); handle=IntPtr.Zero;
    }
  }
  // Receiver.gate serializes Begin/Finish. A failed capture never falls back
  // to a preceding gesture, and a live round's identity is immutable.
  public sealed class KeyTargetBinding {
    readonly KeySink.TargetIdentity fixedTarget;
    readonly Func<KeySink.TargetIdentity> captureForeground;
    KeySink.TargetIdentity active;
    public KeyTargetBinding(KeySink.TargetIdentity fixedTarget,Func<KeySink.TargetIdentity> captureForeground) {
      if((fixedTarget==null)==(captureForeground==null))throw new ArgumentException("exactly_one_target_binding_mode_required");
      this.fixedTarget=fixedTarget;this.captureForeground=captureForeground;
    }
    public bool FollowsForeground {get{return fixedTarget==null;}}
    public KeySink.TargetIdentity CurrentOrFixed {get{return active??fixedTarget;}}
    public KeySink.TargetIdentity BindForBegin() {
      if(active!=null)throw new InvalidOperationException("target_round_already_active");
      var captured=fixedTarget??captureForeground();
      if(captured==null)throw new InvalidOperationException("foreground_identity_unverified; no_previous_target_reused");
      active=captured;return active;
    }
    public void End() {active=null;}
  }
  public sealed class KeySink : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Mouse { public int x,y; public uint data,flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct Key { public ushort vk,scan; public uint flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct Union { [FieldOffset(0)] public Mouse mouse; [FieldOffset(0)] public Key key; }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint type; public Union union; }
    [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint count,Input[] inputs,int size);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint processId);
    [DllImport("kernel32.dll")] static extern IntPtr GetConsoleWindow();
    [DllImport("kernel32.dll",SetLastError=true)] static extern SafeProcessHandle OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageNameW(SafeProcessHandle process,uint flags,StringBuilder name,ref uint size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(SafeProcessHandle process,out long created,out long exited,out long kernel,out long user);
    readonly List<ushort> held=new List<ushort>();
    readonly ushort[] keys;
    readonly TargetIdentity target;
    public sealed class TargetIdentity {
      public int Pid {get;}
      public long Hwnd {get;}
      public DateTime CreatedUtc {get;}
      public string ImagePath {get;}
      public TargetIdentity(int pid,long hwnd,DateTime createdUtc,string imagePath) {
        Pid=pid;Hwnd=hwnd;CreatedUtc=createdUtc;ImagePath=imagePath;
      }
    }
    static ushort Resolve(string name) {
      switch(name) {
        case "LCTRL":return 0xA2; case "RCTRL":return 0xA3; case "LALT":return 0xA4; case "RALT":return 0xA5;
        case "LSHIFT":return 0xA0; case "RSHIFT":return 0xA1; case "LWIN":return 0x5B; case "RWIN":return 0x5C;
        default:int n; if(name!=null&&name.StartsWith("F")&&int.TryParse(name.Substring(1),out n)&&n>=1&&n<=24) return (ushort)(0x70+n-1); throw new InvalidDataException("unsupported_hotkey");
      }
    }
    public KeySink(string[] names,TargetIdentity target) {
      if(target==null||target.Pid<=0||target.Hwnd==0) throw new InvalidDataException("dedicated_test_window_identity_required");
      this.target=target;
      if(names==null||names.Length<1||names.Length>4) throw new InvalidDataException("hotkey_not_configured");
      var k=new List<ushort>(); foreach(string n in names) { ushort v=Resolve(n); if(k.Contains(v)) throw new InvalidDataException("duplicate_hotkey"); k.Add(v); } keys=k.ToArray();
    }
    public bool IsBoundWindowForeground() {
      return MatchesBoundWindow(target);
    }
    public bool IsBoundWindowForeground(out bool foregroundChanged) {return CheckTarget(target,out foregroundChanged);}
    public static TargetIdentity BindForegroundTarget() {
      DateTime gestureUtc=DateTime.UtcNow;
      IntPtr hwnd=GetForegroundWindow();
      uint pid;GetWindowThreadProcessId(hwnd,out pid);
      if(pid==0||pid>int.MaxValue||pid==(uint)Environment.ProcessId||hwnd==IntPtr.Zero||hwnd==GetConsoleWindow())
        throw new InvalidOperationException("missing_or_own_foreground_window");
      return BindWindowTarget((int)pid,hwnd.ToInt64(),gestureUtc);
    }
    static TargetIdentity BindWindowTarget(int pid,long hwnd,DateTime gestureUtc) {
      if(pid<=0||hwnd==0)throw new InvalidDataException("foreground_pid_hwnd_required");
      uint owner;GetWindowThreadProcessId((IntPtr)hwnd,out owner);
      if(owner!=(uint)pid)throw new InvalidOperationException("dedicated_hwnd_owner_mismatch");
      // Identity metadata only, QUERY_LIMITED_INFORMATION; never request VM
      // reads or broader rights for an unqueryable foreground application.
      using(var process=OpenProcess(0x1000,false,pid)) {
        if(process.IsInvalid)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"target_identity_query_denied");
        var imageBuffer=new StringBuilder(32768);uint size=(uint)imageBuffer.Capacity;
        if(!QueryFullProcessImageNameW(process,0,imageBuffer,ref size))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"target_image_query_failed");
        long createdRaw,exited,kernel,user;
        if(!GetProcessTimes(process,out createdRaw,out exited,out kernel,out user))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"target_creation_query_failed");
        string image=imageBuffer.ToString();
        if(string.IsNullOrWhiteSpace(image)||!Path.IsPathFullyQualified(image))
          throw new InvalidOperationException("target_image_unverified");
        DateTime created=DateTime.FromFileTimeUtc(createdRaw);
        if(created>gestureUtc)throw new InvalidOperationException("process_created_after_gesture");
        return new TargetIdentity(pid,hwnd,created,image);
      }
    }
    public static bool SameTargetIdentity(TargetIdentity expected,int pid,long hwnd,DateTime createdUtc,string imagePath) {
      return expected!=null&&expected.Pid>0&&expected.Hwnd!=0&&expected.Pid==pid&&expected.Hwnd==hwnd&&
        expected.CreatedUtc==createdUtc&&
        !string.IsNullOrEmpty(expected.ImagePath)&&
        string.Equals(expected.ImagePath,imagePath,StringComparison.OrdinalIgnoreCase);
    }
    public static bool MatchesBoundWindow(TargetIdentity expected) {
      bool changed;return CheckTarget(expected,out changed);
    }
    static bool CheckTarget(TargetIdentity expected,out bool foregroundChanged) {
      foregroundChanged=false;
      if(expected==null||expected.Pid<=0||expected.Hwnd==0)return false;
      IntPtr window=GetForegroundWindow();
      uint processId;GetWindowThreadProcessId(window,out processId);
      foregroundChanged=window.ToInt64()!=expected.Hwnd||processId!=(uint)expected.Pid;
      if(foregroundChanged)return false;
      try {
        var actual=BindWindowTarget((int)processId,window.ToInt64(),DateTime.MaxValue);
        return SameTargetIdentity(expected,(int)processId,window.ToInt64(),actual.CreatedUtc,actual.ImagePath);
      }
      catch(ArgumentException){return false;}catch(InvalidOperationException){return false;}catch(System.ComponentModel.Win32Exception){return false;}
      catch(UnauthorizedAccessException){return false;}
    }
    static void Send(ushort key,bool up) {
      uint flags=up?2u:0u; if(key==0xA3||key==0xA5||key==0x5B||key==0x5C) flags|=1;
      var keyData=new Key{vk=key,flags=flags};
      // Right Alt is the extended physical E0 38 key. SCANCODE makes wVk unused.
      if(key==0xA5) {keyData.vk=0;keyData.scan=0x38;keyData.flags=0x0008u|0x0001u|(up?0x0002u:0u);}
      var input=new Input{type=1,union=new Union{key=keyData}};
      if(SendInput(1,new[]{input},Marshal.SizeOf(typeof(Input)))!=1) throw new IOException("SendInput_failed:"+Marshal.GetLastWin32Error());
    }
    public void Down() { if(!IsBoundWindowForeground())throw new InvalidOperationException("target_identity_or_foreground_mismatch");try { foreach(ushort k in keys) { Send(k,false); held.Add(k); } } catch { Dispose(); throw; } }
    public void Dispose() { for(int i=held.Count-1;i>=0;i--) { try {Send(held[i],true);} catch {} } held.Clear(); }
  }
  // A known foreground switch ends output immediately, but its authenticated
  // session tail is drained until matching END so the next gesture can reuse TLS.
  public sealed class CancelledSessionTail {
    public string Id {get;private set;}
    object summary;
    public void RequireCanBegin(string active) {if(active!=null||Id!=null)throw new InvalidDataException("session_already_active");}
    public void Remember(string id,object result) {if(Id!=null)throw new InvalidOperationException("cancelled_tail_already_pending");Id=id;summary=result;}
    public bool Matches(string id) {return Id!=null&&Id==id;}
    public bool DiscardAudio(string id,string active) {
      if(Matches(id))return true;
      if(active==null||active!=id)throw new InvalidDataException("session_mismatch");
      return false;
    }
    public object TakeEnd(string id) {if(!Matches(id))throw new InvalidDataException("session_mismatch");return Clear();}
    public object Clear() {var result=summary;Id=null;summary=null;return result;}
  }
  public sealed class Receiver : IDisposable {
    readonly Settings config; readonly string mode,runtime; readonly bool audioLive,keyLive;
    readonly KeyTargetBinding keyTargets;
    readonly CancelledSessionTail cancelledTail=new CancelledSessionTail();
    readonly object gate=new object(),logGate=new object(); readonly CancellationTokenSource stop=new CancellationTokenSource();
    TcpListener listener; TcpClient activeClient; WaveSink audio; KeySink keys; string session;
    long bytes,samples,frames; double squares,peak; DateTime started,lastAudio,lastMessage;
    long totalSessions,totalBytes; bool disposed; readonly int duration;
    public int Port {get;private set;}
    public static void Run(string configPath,string mode,string runtime,int duration) {
      Run(configPath,mode,runtime,duration,true);
    }
    public static void Run(string configPath,string mode,string runtime,int duration,bool followForeground) {
      var c=Configuration.LoadReceiver(configPath);
      using(var server=new Receiver(c,mode,runtime,duration,followForeground)) server.RunLoop();
    }
    public Receiver(Settings c,string mode,string runtime,int duration,bool followForeground=true) {
      if(duration<0||duration>86400) throw new ArgumentOutOfRangeException(nameof(duration));
      if(mode!="diagnostic"&&mode!="audio-live"&&mode!="key-live"&&mode!="audio-key-live") throw new ArgumentException("mode");
      if(c==null||string.IsNullOrEmpty(c.token)||c.token.Length!=64) throw new InvalidDataException("invalid_config");
      config=c;this.mode=mode;this.runtime=runtime;this.duration=duration;
      audioLive=mode=="audio-live"||mode=="audio-key-live"; keyLive=mode=="key-live"||mode=="audio-key-live";
      if(followForeground&&!keyLive)throw new ArgumentException("foreground_mode_requires_key_lifecycle");
      if(keyLive&&(!c.hotkey_verified||c.hotkey==null||c.hotkey.Length==0)) throw new InvalidOperationException("Key-live is disabled until the actual Doubao hold shortcut is verified locally");
      // Bind at each BEGIN; never adopt a reused PID/HWND or preceding target.
      // A changed image must not be silently adopted.

      if(keyLive&&!followForeground)throw new ArgumentException("Per-gesture foreground mode is required");
      if(keyLive)keyTargets=new KeyTargetBinding(null,KeySink.BindForegroundTarget);
      Directory.CreateDirectory(runtime);
    }
    void Log(object value) { lock(logGate) File.AppendAllText(Path.Combine(runtime,"events.jsonl"),JsonSerializer.Serialize(new {utc=DateTime.UtcNow.ToString("o"),details=value})+"\n"); }
    void Status(bool running) {
      lock(gate) {
        string target=Path.Combine(runtime,"status.json"),temp=target+".tmp";
        var bound=keyLive?keyTargets.CurrentOrFixed:null;
        bool follow=keyLive&&keyTargets.FollowsForeground;
        File.WriteAllText(temp,JsonSerializer.Serialize(new {running,mode,bind_address=config.bind_address,port=Port,allowed_peer=config.allowed_peer,pid=Environment.ProcessId,duration_seconds=duration,persistent=duration==0,stop_path=Path.Combine(runtime,"stop.request"),session,cancelled_session=cancelledTail.Id,audio_bytes=bytes,total_sessions=totalSessions,total_bytes=totalBytes,hold_hotkey=keyLive?config.hotkey:null,key_target_mode="gesture-foreground",key_target_bind_on="each BEGIN in worker",key_target_pid=bound!=null?bound.Pid:0,key_target_hwnd=bound!=null?bound.Hwnd:0,key_target_created_utc=bound!=null?(DateTime?)bound.CreatedUtc:null,key_target_image_path=bound!=null?bound.ImagePath:null,key_target_title_is_guard=false,key_target_ready=follow&&bound==null?(bool?)null:keyLive&&KeySink.MatchesBoundWindow(bound),utc=DateTime.UtcNow.ToString("o")}));
        File.Move(temp,target,true);
      }
    }
    object Finish(string reason,bool normal) {
      lock(gate) {
        if(session==null) return cancelledTail.Clear();
        var result=new {type="stats",session,audio_bytes=bytes,samples,frames,duration_ms=samples/48.0,rms=samples>0?Math.Sqrt(squares/samples):0,peak,mode,reason};
        try { if(normal&&audio!=null) audio.Drain(); } finally { if(keys!=null) keys.Dispose(); keys=null; if(audio!=null) audio.Dispose(); audio=null; }
        session=null;if(keyTargets!=null)keyTargets.End();Log(result);return result;
      }
    }
    void Begin(string id) {
      lock(gate) {
        cancelledTail.RequireCanBegin(session);
        bytes=samples=frames=0;squares=peak=0; session=id; started=lastAudio=DateTime.UtcNow;
        try { if(keyLive) {var bound=keyTargets.BindForBegin();keys=new KeySink(config.hotkey,bound);if(!keys.IsBoundWindowForeground())throw new InvalidOperationException("bound_target_identity_or_foreground_mismatch");Log(new{type="target_bound",session,pid=bound.Pid,hwnd=bound.Hwnd,created_utc=bound.CreatedUtc,image_path=bound.ImagePath,scope="gesture-foreground"});} if(audioLive) audio=new WaveSink(config.waveout_name_prefix); if(keyLive) keys.Down(); }
        catch { Finish("begin_failed",false); throw; }
        totalSessions++; Log(new {type="begin",session,mode});
      }
    }
    void Audio(JsonElement e) {
      string id=Framing.Session(e); JsonElement p;
      if(!e.TryGetProperty("pcm",out p)||p.ValueKind!=JsonValueKind.String) throw new InvalidDataException("missing_pcm");
      byte[] pcm=Convert.FromBase64String(p.GetString());
      if(pcm.Length==0||(pcm.Length&1)!=0||pcm.Length>19200) throw new InvalidDataException("pcm_must_be_even_and_at_most_200ms");
      lock(gate) {
        if(cancelledTail.DiscardAudio(id,session)) {lastAudio=DateTime.UtcNow;return;}
        if(samples+pcm.Length/2>48000L*120) throw new InvalidDataException("session_over_120_seconds");
        for(int i=0;i<pcm.Length;i+=2) { double v=(short)(pcm[i]|(pcm[i+1]<<8))/32768.0; squares+=v*v; peak=Math.Max(peak,Math.Abs(v)); }
        samples+=pcm.Length/2; bytes+=pcm.Length;totalBytes+=pcm.Length;frames++;lastAudio=DateTime.UtcNow;
        if(audio!=null) audio.Write(pcm);
      }
    }
    async Task Handle(TcpClient client) {
      using(client) using(var ssl=new SslStream(client.GetStream(),false)) {
        using(var cert=X509CertificateLoader.LoadPkcs12FromFile(config.certificate_path,config.certificate_password,X509KeyStorageFlags.UserKeySet)) {
          using(var handshake=CancellationTokenSource.CreateLinkedTokenSource(stop.Token)) {
            handshake.CancelAfter(5000);
            var options=new SslServerAuthenticationOptions{ServerCertificate=cert,ClientCertificateRequired=false,EnabledSslProtocols=SslProtocols.Tls12|SslProtocols.Tls13,CertificateRevocationCheckMode=X509RevocationMode.NoCheck};
            await ssl.AuthenticateAsServerAsync(options,handshake.Token).ConfigureAwait(false);
          }
        }
        using(var first=CancellationTokenSource.CreateLinkedTokenSource(stop.Token)) {
          first.CancelAfter(5000);
          using(var hello=await Framing.Read(ssl,first.Token).ConfigureAwait(false)) {
            var h=hello.RootElement; JsonElement proto,t;
            if(Framing.Type(h)!="hello"||!h.TryGetProperty("protocol",out proto)||proto.GetInt32()!=1||!h.TryGetProperty("token",out t)||t.ValueKind!=JsonValueKind.String||!Framing.TokenEquals(config.token,t.GetString())) {
              await Framing.Write(ssl,new{type="error",error="authentication_failed"},first.Token).ConfigureAwait(false);
              throw new InvalidDataException("authentication_failed");
            }
          }
          await Framing.Write(ssl,new{type="hello",protocol=1,sample_rate=48000,channels=1,format="s16le",mode},first.Token).ConfigureAwait(false);
        }
        lock(gate) lastMessage=DateTime.UtcNow;
        Log(new{type="authenticated",mode});
        while(!stop.IsCancellationRequested) {
          using(var timeout=CancellationTokenSource.CreateLinkedTokenSource(stop.Token)) {
            timeout.CancelAfter(5000);
            using(var doc=await Framing.Read(ssl,timeout.Token).ConfigureAwait(false)) {
              var e=doc.RootElement; string type=Framing.Type(e); lock(gate) lastMessage=DateTime.UtcNow;
              if(type=="ping") await Framing.Write(ssl,new{type="pong"},timeout.Token).ConfigureAwait(false);
              else if(type=="begin") Begin(Framing.Session(e));
              else if(type=="audio") Audio(e);
              else if(type=="end") {
                string id=Framing.Session(e);object result;
                lock(gate) {
                  if(cancelledTail.Matches(id)) {result=cancelledTail.TakeEnd(id);Log(new{type="cancelled_end",session=id});}
                  else {if(session==null||session!=id)throw new InvalidDataException("session_mismatch");result=Finish("end",true);}
                }
                await Framing.Write(ssl,result,timeout.Token).ConfigureAwait(false);
              }
              else if(type=="cancel"||type=="route") {
                var result=Finish(type,false); if(result!=null) await Framing.Write(ssl,result,timeout.Token).ConfigureAwait(false);
                if(type=="route") break; // Any route change closes the connection; a fresh hello is required.
              }
              else throw new InvalidDataException("unsupported_message");
            }
          }
        }
      }
    }
    public void RunLoop() {
      string stopPath=Path.Combine(runtime,"stop.request");
      if(File.Exists(stopPath)) throw new InvalidOperationException("Remove the existing stop.request explicitly before starting a new receiver");
      listener=new TcpListener(IPAddress.Parse(config.bind_address),config.port);listener.Start(4);Port=((IPEndPoint)listener.LocalEndpoint).Port;
      Log(new{type="listening",mode,bind=config.bind_address,port=Port});Status(true);
      Task clientTask=null; var lifetime=Stopwatch.StartNew(); DateTime statusAt=DateTime.MinValue;
      try {
        while(!stop.IsCancellationRequested&&(duration==0||lifetime.Elapsed.TotalSeconds<duration)&&!File.Exists(stopPath)) {
          if(clientTask!=null&&clientTask.IsCompleted) {
            try {clientTask.GetAwaiter().GetResult();} catch(Exception ex) {Log(new{type="client_closed",error=ex.GetType().Name,detail=ex.GetBaseException().Message});}
            Finish("disconnect",false);lock(gate) activeClient=null;clientTask=null;
          }
          if(listener.Pending()) {
            var candidate=listener.AcceptTcpClient(); string peer=((IPEndPoint)candidate.Client.RemoteEndPoint).Address.ToString();
            if(peer!=config.allowed_peer||clientTask!=null) { candidate.Dispose();Log(new{type="rejected",reason=peer!=config.allowed_peer?"peer":"busy"}); }
            else {candidate.NoDelay=true;lock(gate) {activeClient=candidate;lastMessage=DateTime.UtcNow;} clientTask=Task.Run(()=>Handle(candidate));}
          }
          lock(gate) {
            bool foregroundChanged;
            if(keys!=null&&!keys.IsBoundWindowForeground(out foregroundChanged)) {
              bool knownSwitch=keyTargets.FollowsForeground&&foregroundChanged;
              string oldSession=session;var result=Finish("test_window_changed",false);
              if(knownSwitch&&oldSession!=null)cancelledTail.Remember(oldSession,result);
              else if(activeClient!=null)activeClient.Dispose();
            }
            if((session!=null||cancelledTail.Id!=null)&&((DateTime.UtcNow-lastAudio).TotalMilliseconds>2000||(DateTime.UtcNow-started).TotalSeconds>120)) {Finish("audio_timeout",false);if(activeClient!=null) activeClient.Dispose();}
            if(activeClient!=null&&(DateTime.UtcNow-lastMessage).TotalSeconds>7) {Finish("connection_timeout",false);activeClient.Dispose();}
            if(audio!=null) audio.Reap();
          }
          if((DateTime.UtcNow-statusAt).TotalSeconds>=1) {Status(true);statusAt=DateTime.UtcNow;}
          Thread.Sleep(50);
        }
      } finally { stop.Cancel(); if(activeClient!=null) activeClient.Dispose();Finish("receiver_stopped",false);listener.Stop();if(clientTask!=null) {try {clientTask.Wait(2000);} catch {}} Status(false);Log(new{type="stopped"}); }
    }
    public void RequestStop() {stop.Cancel();}
    public void Dispose() {if(disposed)return;disposed=true;stop.Cancel();if(activeClient!=null)activeClient.Dispose();Finish("dispose",false);if(listener!=null)listener.Stop();stop.Dispose();}
  }
}
