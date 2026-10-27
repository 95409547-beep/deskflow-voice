// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#nullable disable
using System;
using System.IO;
using System.Collections.Generic;
using System.Text.Json;

namespace DeskflowVoice.Windows {
  public static class ForegroundReceiverChecks {
    public static string Run() {
      var checks=new Dictionary<string,bool>();
      DateTime time=new DateTime(2026,1,1,0,0,0,DateTimeKind.Utc);
      var appA=new KeySink.TargetIdentity(101,1001,time,@"C:\Apps\First.exe");
      var appB=new KeySink.TargetIdentity(202,2002,time,@"C:\Program Files\PowerShell\7\pwsh.exe");
      KeySink.TargetIdentity foreground=appA;int captures=0;
      var binding=new KeyTargetBinding(null,()=>{captures++;if(foreground==null)throw new InvalidOperationException("unverified");return foreground;});
      checks["generic_idle_has_no_previous_target_or_notepad_dependency"]=binding.FollowsForeground&&binding.CurrentOrFixed==null&&captures==0;
      var first=binding.BindForBegin();foreground=appB;
      checks["active_round_keeps_original_identity_when_foreground_changes"]=ReferenceEquals(binding.CurrentOrFixed,first)&&first.Pid==101&&captures==1;
      binding.End();var second=binding.BindForBegin();
      checks["next_begin_naturally_binds_different_application"]=second.Pid==202&&captures==2;
      checks["normal_non_notepad_and_pwsh_paths_allowed"]=KeySink.SameTargetIdentity(second,202,2002,time,appB.ImagePath);
      binding.End();foreground=null;bool failed=false;
      try{binding.BindForBegin();}catch(InvalidOperationException){failed=true;}
      checks["failed_begin_does_not_reuse_preceding_app"]=failed&&binding.CurrentOrFixed==null;
      foreground=appA;
      checks["fresh_begin_after_rejection_captures_again"]=binding.BindForBegin().Pid==101&&captures==4;
      binding.End();
      checks["other_window_same_app_rejected"]=!KeySink.SameTargetIdentity(appA,101,1002,time,appA.ImagePath);
      checks["other_process_rejected"]=!KeySink.SameTargetIdentity(appA,202,1001,time,appA.ImagePath);
      checks["reused_pid_creation_rejected"]=!KeySink.SameTargetIdentity(appA,101,1001,time.AddTicks(1),appA.ImagePath);
      checks["changed_image_rejected"]=!KeySink.SameTargetIdentity(appA,101,1001,time,@"C:\Other\First.exe");

      var tail=new CancelledSessionTail();
      var oldStats=new{type="stats",session="old-session",reason="test_window_changed"};
      binding.BindForBegin();binding.End();tail.Remember("old-session",oldStats);
      bool blockedBeforeEnd=false;try{tail.RequireCanBegin(null);}catch(InvalidDataException){blockedBeforeEnd=true;}
      bool wrongAudioRejected=false;try{tail.DiscardAudio("wrong-session",null);}catch(InvalidDataException){wrongAudioRejected=true;}
      checks["holding_switch_discards_only_cancelled_audio_and_keeps_end_pending"]=tail.DiscardAudio("old-session",null)&&blockedBeforeEnd&&wrongAudioRejected;
      bool wrongEndRejected=false;try{tail.TakeEnd("wrong-session");}catch(InvalidDataException){wrongEndRejected=true;}
      var cancelledResult=tail.TakeEnd("old-session");tail.RequireCanBegin(null);foreground=appB;
      checks["matching_end_after_holding_switch_allows_immediate_next_begin"]=wrongEndRejected&&ReferenceEquals(cancelledResult,oldStats)&&tail.Id==null&&binding.BindForBegin().Pid==202;
      binding.End();foreground=appA;
      // Normal release/END completes A before foreground becomes B; no tail or
      // reconnect delay is needed, and the same binding manager accepts B.
      binding.BindForBegin();binding.End();foreground=appB;tail.RequireCanBegin(null);
      checks["release_then_quick_window_switch_rebinds_without_cancelled_tail"]=binding.BindForBegin().Pid==202&&tail.Id==null;
      binding.End();tail.Remember("disconnected",oldStats);tail.Clear();tail.RequireCanBegin(null);
      checks["disconnect_clears_cancelled_tail_without_adopting_old_target"]=tail.Id==null&&binding.CurrentOrFixed==null;

      string runtime=Path.Combine(Path.GetTempPath(),"Deskflow-foreground-constructor-"+Guid.NewGuid().ToString("N"));
      try {
        // Deliberately invalid old fixed-target fields. Construction must not
        // query or require a Notepad process, and never starts a listener/device.
        var settings=new Settings{token=new string('0',64),hotkey_verified=true,hotkey=new[]{"RALT"}};
        using(var receiver=new Receiver(settings,"audio-key-live",runtime,0,true)) {
          checks["generic_receiver_constructs_without_any_fixed_app_or_pid"]=receiver.Port==0;
        }
      } finally {if(Directory.Exists(runtime))Directory.Delete(runtime);}

      foreach(var check in checks)if(!check.Value)throw new Exception("Foreground receiver check failed: "+check.Key);
      return JsonSerializer.Serialize(new{checks,real_windows_queried=false,document_read=false,keys_sent=false,rpc_called=false,audio_device_opened=false,listener_started=false});
    }
  }
}
