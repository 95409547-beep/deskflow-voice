// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal static class V2ProtocolChecks
{
    private sealed class Fake : IV2VoiceBackend
    {
        internal int Begins, Writes, Stops, Drains;
        internal bool Busy, FailBegin, FailEnd;
        internal string? Cancel;
        internal readonly List<string> Order = new();
        public bool Ready { get; internal set; } = true;
        public void Begin(V2Begin begin)
        {
            if (Busy) throw new V2BusyException("busy");
            Begins++; Order.Add("start-show-mic-started");
            if (FailBegin) { Ready = false; throw new RepeatRpcUnknownException("fake unknown"); }
        }
        public void Audio(byte[] pcm) { Writes++; Order.Add("audio"); }
        public void End(string reason, bool drain)
        {
            if (drain) { Drains++; Order.Add("drain"); }
            Stops++; Order.Add("stop");
            if (FailEnd) { Ready = false; throw new InvalidOperationException("fake mic pending"); }
            Order.Add("mic-ended");
        }
        public string? CancellationReason() => Cancel;
        public void Dispose() { }
    }
    private sealed class Fixture
    {
        internal readonly Fake Backend = new(); internal long Now;
        internal readonly V2Protocol Protocol;
        internal Fixture() { Protocol = new V2Protocol(Backend, () => Now); }
        internal IReadOnlyList<Dictionary<string, object>> Send(object value)
        {
            using var document = JsonDocument.Parse(JsonSerializer.Serialize(value)); return Protocol.Process(document.RootElement);
        }
        internal IReadOnlyList<Dictionary<string, object>> Begin(string id, string sequence = "1", string generation = "1") =>
            Send(new { type = "begin", epoch = Protocol.Epoch, session = id, sequence, generation });
        internal IReadOnlyList<Dictionary<string, object>> End(string id) => Send(new { type = "end", epoch = Protocol.Epoch, session = id });
        internal IReadOnlyList<Dictionary<string, object>> Audio(string id, int count = 1920) =>
            Send(new { type = "audio", epoch = Protocol.Epoch, session = id, pcm = Convert.ToBase64String(new byte[count]) });
    }
    internal static Dictionary<string, bool> Run()
    {
        var checks = new Dictionary<string, bool>();
        void Check(string name, bool passed) { checks.Add("v2:" + name, passed); if (!passed) throw new InvalidOperationException("V2 check failed: " + name); }
        static string Id() => Guid.NewGuid().ToString("D");
        static bool Type(IReadOnlyList<Dictionary<string, object>> replies, string type) => replies.Count == 1 && (string)replies[0]["type"] == type;
        var token = new string('a', 64);
        using (var good = JsonDocument.Parse(JsonSerializer.Serialize(new { type = "hello", protocol = 2, token })))
        using (var old = JsonDocument.Parse(JsonSerializer.Serialize(new { type = "hello", protocol = 1, token })))
        {
            Check("hello-v2-authenticates", V2Protocol.Authenticate(good.RootElement, token));
            Check("hello-old-version-rejected", !V2Protocol.Authenticate(old.RootElement, token));
            Check("hello-wrong-token-rejected", !V2Protocol.Authenticate(good.RootElement, new string('b', 64)));
        }
        using (var scalar = JsonDocument.Parse("null"))
        using (var stringVersion = JsonDocument.Parse(JsonSerializer.Serialize(new { type = "hello", protocol = "2", token })))
        {
            Check("hello-non-object-rejected", !V2Protocol.Authenticate(scalar.RootElement, token));
            Check("hello-version-must-be-a-number", !V2Protocol.Authenticate(stringVersion.RootElement, token));
            var malformed = new Fixture();
            Check("non-object-message-has-no-backend-actions", Type(malformed.Send(new[] { 1 }), "error") && malformed.Backend.Begins == 0 && malformed.Backend.Stops == 0);
        }
        var a = new Fixture(); var first = Id(); var second = Id(); var hello = a.Protocol.Hello();
        var unprepared = new Fixture(); unprepared.Backend.Ready = false;
        Check("hello-never-assumes-unprepared-backend-is-ready", !(bool)unprepared.Protocol.Hello()["ready"] && unprepared.Backend.Begins == 0 && unprepared.Backend.Stops == 0);
        Check("unprepared-backend-cannot-acknowledge-begin", Type(unprepared.Begin(Id()), "error") && unprepared.Backend.Begins == 0);
        Check("hello-epoch-and-wire-format", Guid.TryParse((string)hello["epoch"], out _) && (int)hello["protocol"] == 2 && (int)hello["sample_rate"] == 48000 && (int)hello["channels"] == 1 && (string)hello["format"] == "s16le");
        Check("started-after-backend-confirmation", Type(a.Begin(first), "started") && a.Backend.Begins == 1 && a.Backend.Order.SequenceEqual(new[] { "start-show-mic-started" }));
        Check("duplicate-begin-idempotent", Type(a.Begin(first), "started") && a.Backend.Begins == 1);
        Check("changed-active-identity-rejected", Type(a.Begin(first, "2"), "error") && a.Backend.Begins == 1);
        Check("other-begin-cannot-preempt-active", Type(a.Begin(second, "2"), "error") && a.Backend.Stops == 0);
        a.Audio(first); var normal = a.End(first);
        Check("normal-end-drains-before-stop-and-ready", Type(normal, "ended") && (bool)normal[0]["ready"] && a.Backend.Order.SequenceEqual(new[] { "start-show-mic-started", "audio", "drain", "stop", "mic-ended" }));
        Check("end-counts-only-accepted-pcm", (long)normal[0]["audio_bytes"] == 1920 && (long)normal[0]["frames"] == 1);
        Check("duplicate-end-does-not-stop-again", Type(a.End(first), "ended") && a.Backend.Stops == 1);
        a.Protocol.Close(); Check("close-after-end-does-not-stop-again", a.Backend.Stops == 1);
        Check("next-new-session-starts", Type(a.Begin(second, "2", "1"), "started") && a.Backend.Begins == 2);
        Check("old-completed-audio-not-written-into-new-session", a.Audio(first).Count == 0 && a.Backend.Writes == 1);
        Check("old-end-cannot-stop-new-session", Type(a.End(first), "ended") && a.Backend.Stops == 1);
        a.End(second);
        Check("sequence-replay-rejected", Type(a.Begin(Id(), "2"), "error") && a.Backend.Begins == 2);
        Check("completed-session-uuid-cannot-be-reused", Type(a.Begin(first, "3"), "error"));
        Check("large-decimal-sequence-without-integer-overflow", Type(a.Begin(Id(), "18446744073709551616000"), "started"));
        a.Protocol.Close();
        var wrong = new Fixture(); var wrongId = Id();
        Check("wrong-epoch-rejected-before-backend", Type(wrong.Send(new { type = "begin", epoch = Id(), session = wrongId, sequence = "1", generation = "1" }), "error") && wrong.Backend.Begins == 0);
        Check("missing-epoch-rejected", Type(wrong.Send(new { type = "ping" }), "error"));
        var pong = wrong.Send(new { type = "ping", epoch = wrong.Protocol.Epoch });
        Check("ping-pong-carries-current-epoch", Type(pong, "pong") && (string)pong[0]["epoch"] == wrong.Protocol.Epoch);
        Check("zero-generation-rejected", Type(wrong.Begin(Id(), "1", "0"), "error"));
        Check("leading-zero-sequence-rejected", Type(wrong.Begin(Id(), "01"), "error"));
        Check("numeric-sequence-must-be-a-string", Type(wrong.Send(new { type = "begin", epoch = wrong.Protocol.Epoch, session = Id(), sequence = 1, generation = "1" }), "error"));
        var pcm = new Fixture(); var pcmId = Id(); pcm.Begin(pcmId);
        Check("maximum-audio-frame-accepted", pcm.Audio(pcmId, 19200).Count == 0 && pcm.Backend.Writes == 1);
        Check("oversized-audio-rejected", Type(pcm.Audio(pcmId, 19202), "error") && pcm.Backend.Writes == 1);
        Check("odd-audio-rejected", Type(pcm.Audio(pcmId, 3), "error"));
        Check("empty-audio-rejected", Type(pcm.Audio(pcmId, 0), "error"));
        Check("malformed-base64-rejected", Type(pcm.Send(new { type = "audio", epoch = pcm.Protocol.Epoch, session = pcmId, pcm = "%%%" }), "error"));
        Check("unknown-session-audio-rejected", Type(pcm.Audio(Id()), "error"));
        Check("stale-epoch-audio-is-not-written", Type(pcm.Send(new { type = "audio", epoch = Id(), session = pcmId, pcm = "AAA=" }), "error") && pcm.Backend.Writes == 1);
        pcm.Protocol.Close(); pcm.Protocol.Close(); Check("disconnect-stop-once", pcm.Backend.Stops == 1 && pcm.Backend.Drains == 0);
        var cancel = new Fixture(); var cancelledId = Id(); cancel.Begin(cancelledId); cancel.Backend.Cancel = "foreground_changed";
        var proactive = cancel.Protocol.Tick();
        Check("foreground-change-proactively-ends", Type(proactive, "ended") && (string)proactive[0]["reason"] == "foreground_changed" && cancel.Backend.Stops == 1);
        Check("cancelled-tail-audio-discarded", cancel.Audio(cancelledId).Count == 0 && cancel.Backend.Writes == 0);
        Check("cancel-tick-and-end-idempotent", cancel.Protocol.Tick().Count == 0 && Type(cancel.End(cancelledId), "ended") && cancel.Backend.Stops == 1);
        cancel.Backend.Cancel = null; Check("new-begin-after-cancel-without-reconnect", Type(cancel.Begin(Id(), "2"), "started")); cancel.Protocol.Close();
        var timeout = new Fixture(); timeout.Begin(Id()); timeout.Now = 1999;
        Check("audio-timeout-not-early", timeout.Protocol.Tick().Count == 0);
        timeout.Now = 2000; var timed = timeout.Protocol.Tick();
        Check("audio-timeout-ends-at-two-seconds", Type(timed, "ended") && (string)timed[0]["reason"] == "audio_timeout_2s" && timeout.Backend.Stops == 1);
        var hold = new Fixture(); var holdId = Id(); hold.Begin(holdId);
        for (var i = 1; i <= 60; i++) { hold.Now = i * 1000; hold.Audio(holdId, 2); }
        var limited = hold.Protocol.Tick();
        Check("sixty-second-hold-limit-despite-audio", Type(limited, "ended") && (string)limited[0]["reason"] == "maximum_hold_60s");
        var busy = new Fixture(); busy.Backend.Busy = true;
        Check("busy-legacy-microphone-does-not-start-or-stop", Type(busy.Begin(Id()), "error") && busy.Backend.Begins == 0 && busy.Backend.Stops == 0);
        var detailedBusy = busy.Begin(Id(), "2");
        Check("busy-reply-keeps-compatible-error-and-specific-reason", (string)detailedBusy[0]["error"] == "microphone_busy" && (string)detailedBusy[0]["reason"] == "busy");
        var unreadable = new Fixture(); unreadable.Begin(Id()); unreadable.Backend.Cancel = "legacy_receiver_sharing_violation";
        var unreadableEnd = unreadable.Protocol.Tick();
        Check("unreadable-legacy-status-not-labelled-active-session", Type(unreadableEnd, "ended") && (string)unreadableEnd[0]["reason"] == "legacy_receiver_sharing_violation" && unreadable.Backend.Stops == 1);
        var failure = new Fixture(); failure.Backend.FailBegin = true;
        var failedStart = failure.Begin(Id());
        Check("failed-start-has-no-started-ack", Type(failedStart, "error") && !(bool)failedStart[0]["ready"]);
        Check("unknown-backend-cannot-start-next-session", Type(failure.Begin(Id(), "2"), "error") && failure.Backend.Begins == 1);
        var ending = new Fixture(); var endingId = Id(); ending.Begin(endingId); ending.Backend.FailEnd = true;
        var notReady = ending.End(endingId);
        Check("mic-end-failure-never-reports-ready", Type(notReady, "ended") && !(bool)notReady[0]["ready"]);
        ending.End(endingId); ending.Protocol.Close(); Check("failed-stop-has-no-automatic-repeat", ending.Backend.Stops == 1);
        Check("cli-v2-requires-explicit-private-config", CommandLine.Parse(["--serve-v2"]) == Command.Invalid && CommandLine.Parse(["--serve-v2", "private.local.json"]) == Command.ServeV2);
        return checks;
    }
}
