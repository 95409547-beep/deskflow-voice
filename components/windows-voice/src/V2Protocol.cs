// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal sealed record V2Begin(string Session, string Sequence, string Generation);
internal sealed class V2ProtocolException(string code) : Exception(code) { internal string Code => Message; }
internal sealed class V2BusyException(string code) : Exception(code);
internal sealed class V2PreflightException(string code) : Exception(code) { internal string Code => Message; }
internal interface IV2VoiceBackend : IDisposable
{
    bool Ready { get; }
    void Begin(V2Begin begin);
    void Audio(byte[] pcm);
    void End(string reason, bool drain);
    string? CancellationReason();
}

// One connection/epoch, used only by a serial worker. No OS/device operations.
internal sealed class V2Protocol
{
    internal const int MaximumAudioBytes = 19200;
    private readonly IV2VoiceBackend backend;
    private readonly Func<long> clock;
    private readonly Dictionary<string, Dictionary<string, object>> ended = new();
    private readonly HashSet<string> seen = new();
    private readonly Queue<string> history = new();
    private V2Begin? active;
    private string lastSequence = "0";
    private long began, lastAudio, bytes, frames;
    private bool quarantined;
    internal string Epoch { get; }

    internal V2Protocol(IV2VoiceBackend backend, Func<long> clock, string? epoch = null)
    {
        this.backend = backend; this.clock = clock; Epoch = epoch ?? Guid.NewGuid().ToString("D");
    }
    internal Dictionary<string, object> Hello() => new()
    {
        ["type"] = "hello", ["protocol"] = 2, ["epoch"] = Epoch, ["receiver"] = "windows", ["ready"] = !quarantined && backend.Ready,
        ["sample_rate"] = 48000, ["channels"] = 1, ["format"] = "s16le"
    };
    internal static bool Authenticate(JsonElement hello, string token) =>
        hello.ValueKind == JsonValueKind.Object &&
        hello.TryGetProperty("type", out var type) && type.ValueKind == JsonValueKind.String && type.GetString() == "hello" &&
        hello.TryGetProperty("protocol", out var version) && version.ValueKind == JsonValueKind.Number && version.TryGetInt32(out var protocol) && protocol == 2 &&
        hello.TryGetProperty("token", out var supplied) && supplied.ValueKind == JsonValueKind.String && Framing.TokenEquals(token, supplied.GetString());
    internal IReadOnlyList<Dictionary<string, object>> Process(JsonElement message)
    {
        var replies = new List<Dictionary<string, object>>();
        try
        {
            if (message.ValueKind != JsonValueKind.Object) throw new V2ProtocolException("invalid_message");
            if (Uuid(message, "epoch") != Epoch) throw new V2ProtocolException("epoch_mismatch");
            switch (Text(message, "type"))
            {
                case "ping": replies.Add(Reply("pong")); break;
                case "begin": Begin(message, replies); break;
                case "audio": Audio(message); break;
                case "end":
                    var id = Uuid(message, "session");
                    if (active?.Session == id) replies.Add(Finish("end", true));
                    else if (ended.TryGetValue(id, out var result)) replies.Add(result);
                    else throw new V2ProtocolException("unknown_session");
                    break;
                default: throw new V2ProtocolException("unsupported_message");
            }
        }
        catch (V2ProtocolException error) { replies.Add(Error(error.Code)); }
        catch (V2BusyException) { replies.Add(Error("microphone_busy")); }
        catch (RepeatTargetRejectedException)
        {
            if (active is not null) replies.Add(Finish("foreground_changed", false));
            else replies.Add(Error("foreground_changed"));
        }
        catch (Exception)
        {
            if (active is not null) replies.Add(Finish("backend_error", false));
            quarantined |= !backend.Ready;
            replies.Add(Error("backend_error"));
        }
        return replies;
    }
    private void Begin(JsonElement message, List<Dictionary<string, object>> replies)
    {
        var requested = new V2Begin(Uuid(message, "session"), Decimal(message, "sequence"), Decimal(message, "generation"));
        if (active?.Session == requested.Session)
        {
            if (active != requested) throw new V2ProtocolException("session_identity_changed");
            replies.Add(Started(requested.Session)); return;
        }
        if (active is not null) throw new V2ProtocolException("session_active");
        if (seen.Contains(requested.Session)) throw new V2ProtocolException("session_reused");
        if (!Greater(requested.Sequence, lastSequence)) throw new V2ProtocolException("sequence_replay");
        if (quarantined || !backend.Ready) throw new V2BusyException("backend_not_ready");
        lastSequence = requested.Sequence; Remember(requested.Session);
        var now = clock();
        backend.Begin(requested); // returns only after Start/Show and fresh microphone confirmation
        active = requested; began = now; lastAudio = clock(); bytes = frames = 0;
        replies.Add(Started(requested.Session));
    }
    private void Audio(JsonElement message)
    {
        var id = Uuid(message, "session");
        if (ended.ContainsKey(id)) return; // cancelled/completed tail, including after a newer begin
        if (active?.Session != id) throw new V2ProtocolException("unknown_session");
        byte[] pcm;
        try { pcm = Convert.FromBase64String(Text(message, "pcm")); }
        catch (FormatException) { throw new V2ProtocolException("invalid_pcm_base64"); }
        if (pcm.Length is 0 or > MaximumAudioBytes || (pcm.Length & 1) != 0) throw new V2ProtocolException("invalid_pcm_length");
        backend.Audio(pcm); bytes += pcm.Length; frames++; lastAudio = clock();
    }
    internal IReadOnlyList<Dictionary<string, object>> Tick()
    {
        if (active is null) return Array.Empty<Dictionary<string, object>>();
        try
        {
            var reason = backend.CancellationReason();
            var now = clock();
            if (reason is null && now - began >= 60000) reason = "maximum_hold_60s";
            if (reason is null && now - lastAudio >= 2000) reason = "audio_timeout_2s";
            return reason is null ? Array.Empty<Dictionary<string, object>>() : new[] { Finish(reason, false) };
        }
        catch { return new[] { Finish("backend_error", false) }; }
    }
    internal void Close()
    {
        if (active is not null) Finish("connection_closed", false);
    }
    private Dictionary<string, object> Finish(string reason, bool drain)
    {
        var session = active ?? throw new InvalidOperationException("No active session");
        active = null; // finally/end/disconnect/tick cannot stop the same ownership twice
        try { backend.End(reason, drain); }
        catch { quarantined = true; }
        var ready = !quarantined && backend.Ready;
        var reply = Reply("ended");
        reply["session"] = session.Session; reply["ready"] = ready; reply["reason"] = ready ? reason : "backend_not_ready";
        reply["audio_bytes"] = bytes; reply["frames"] = frames;
        ended[session.Session] = reply;
        return reply;
    }
    private void Remember(string id)
    {
        seen.Add(id); history.Enqueue(id);
        while (history.Count > 64) { var old = history.Dequeue(); seen.Remove(old); ended.Remove(old); }
    }
    private Dictionary<string, object> Started(string id)
    {
        var reply = Reply("started"); reply["session"] = id; return reply;
    }
    private Dictionary<string, object> Error(string code)
    {
        var reply = Reply("error"); reply["error"] = code; reply["ready"] = !quarantined && backend.Ready; return reply;
    }
    private Dictionary<string, object> Reply(string type) => new() { ["type"] = type, ["epoch"] = Epoch };
    internal static string Text(JsonElement message, string name)
    {
        if (!message.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.String)
            throw new V2ProtocolException("missing_" + name);
        return value.GetString()!;
    }
    internal static string Uuid(JsonElement message, string name) => Guid.TryParse(Text(message, name), out var id)
        ? id.ToString("D") : throw new V2ProtocolException("invalid_" + name);
    private static string Decimal(JsonElement message, string name)
    {
        var text = Text(message, name);
        if (text.Length == 0 || text[0] is < '1' or > '9' || text.Any(c => c is < '0' or > '9'))
            throw new V2ProtocolException("invalid_" + name);
        return text;
    }
    private static bool Greater(string left, string right) => left.Length > right.Length ||
        (left.Length == right.Length && string.CompareOrdinal(left, right) > 0);
}
