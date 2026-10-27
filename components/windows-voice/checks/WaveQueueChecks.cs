// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#nullable disable
using System;
using System.IO;
using System.Collections.Generic;
using System.Text.Json;

namespace DeskflowVoice.Windows {
  // Fake waveOut completions and a virtual clock. No WaveSink instance, native
  // device, receiver, keyboard, microphone, TLS or RPC is used by these checks.
  public static class WaveQueueChecks {
    sealed class FakeWaveOut {
      sealed class Buffer { public int bytes; public long doneAt; }
      readonly List<Buffer> pending=new List<Buffer>();
      public long now,tail,waited;
      public int peakQueued,submitted,waitCalls,startupDelayMs;
      public bool frozen,oversleep;
      public int Reap() {
        if(!frozen)pending.RemoveAll(b=>b.doneAt<=now);
        int queued=0;foreach(var b in pending)queued+=b.bytes;return queued;
      }
      public void Wait(int ms) {waitCalls++;waited+=ms;now+=ms;if(oversleep)now+=600;}
      public void Silence(byte[] mono) {
        foreach(byte value in mono)if(value!=0)throw new Exception("Only generated silence is allowed in these checks");
        int stereoBytes=mono.Length*2;
        WaveQueueBackpressure.WaitForCapacity(stereoBytes,Reap,()=>now,Wait);
        if(submitted==0)tail=now+startupDelayMs;
        tail=Math.Max(tail,now)+stereoBytes/192; // all tested blocks are exact ms
        pending.Add(new Buffer{bytes=stereoBytes,doneAt=tail});submitted++;
        peakQueued=Math.Max(peakQueued,Reap());
        if(peakQueued>WaveQueueBackpressure.CapacityBytes)throw new Exception("queue cap exceeded");
      }
    }
    public static string Run() {
      var checks=new Dictionary<string,bool>();
      byte[] silence20ms=new byte[1920]; // mono 48000Hz * 2 bytes * 0.020s
      byte[] silence200ms=new byte[19200];

      var normal=new FakeWaveOut();
      for(int i=0;i<100;i++){normal.Silence(silence20ms);normal.now+=20;}
      checks["normal_rate_100_silent_frames_no_wait_or_drop"]=normal.submitted==100&&normal.waitCalls==0&&normal.peakQueued<=48000;

      var burst=new FakeWaveOut();
      for(int i=0;i<30;i++)burst.Silence(silence20ms);
      checks["600ms_burst_backpressured_without_raising_250ms_cap"]=burst.submitted==30&&burst.waited>0&&burst.peakQueued<=48000;
      burst.now=burst.tail;
      checks["completed_buffers_reclaimed_to_zero"]=burst.Reap()==0;

      var startup=new FakeWaveOut{startupDelayMs=120};
      for(int i=0;i<30;i++)startup.Silence(silence20ms);
      checks["120ms_startup_delay_and_burst_survive"]=startup.submitted==30&&startup.waited>burst.waited&&startup.peakQueued<=48000;

      var large=new FakeWaveOut();
      large.Silence(silence200ms);large.Silence(silence200ms);
      checks["maximum_200ms_packets_wait_for_whole_buffer_completion"]=large.submitted==2&&large.waited==200&&large.peakQueued==38400;

      var stalled=new FakeWaveOut{frozen=true};
      for(int i=0;i<12;i++)stalled.Silence(silence20ms);
      bool timedOut=false;
      try{stalled.Silence(silence20ms);}catch(IOException e){timedOut=e.Message=="audio_output_backpressure_timeout_500ms";}
      checks["stalled_device_times_out_500ms_without_submitting_extra_frame"]=timedOut&&stalled.waited==500&&stalled.submitted==12&&stalled.peakQueued<=48000;

      var late=new FakeWaveOut();
      for(int i=0;i<12;i++)late.Silence(silence20ms);
      late.oversleep=true;
      bool lateRejected=false;
      try{late.Silence(silence20ms);}catch(IOException e){lateRejected=e.Message=="audio_output_backpressure_timeout_500ms";}
      checks["completion_after_deadline_does_not_reset_or_bypass_timeout"]=lateRejected&&late.submitted==12;

      var oversized=new FakeWaveOut();
      bool oversizedRejected=false;
      try{oversized.Silence(new byte[25000]);}catch(IOException e){oversizedRejected=e.Message=="audio_frame_exceeds_250ms_capacity";}
      checks["single_frame_larger_than_cap_fails_without_wait"]=oversizedRejected&&oversized.waitCalls==0&&oversized.submitted==0;

      foreach(var result in checks)if(!result.Value)throw new Exception("Wave queue check failed: "+result.Key);
      return JsonSerializer.Serialize(new{checks,format="generated mono s16le silence; simulated stereo byte count",capacity_bytes=WaveQueueBackpressure.CapacityBytes,capacity_ms=250,wait_timeout_ms=WaveQueueBackpressure.WaitTimeoutMs,
        normal_frames=normal.submitted,burst_frames=burst.submitted,burst_wait_ms=burst.waited,burst_peak_bytes=burst.peakQueued,startup_wait_ms=startup.waited,stalled_wait_ms=stalled.waited,
        real_device_opened=false,native_waveout_called=false,keyboard_sent=false,rpc_called=false,microphone_opened=false,receiver_started=false});
    }
  }
}
