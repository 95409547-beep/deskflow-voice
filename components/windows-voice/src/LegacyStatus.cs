// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text;
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal enum LegacyStatusKind { Receiver, Controller }
internal sealed record LegacyStatusResult(bool Idle, string Code, int Attempts, long ElapsedMs);
internal sealed record LegacyStatusObservation(string Code, int Attempt, long ElapsedMs, bool Retrying, string? Field, int? IoHResult);

// Parsing and retry policy have no OS dependencies; file/clock/delay are injected.
internal static class LegacyStatus
{
    internal const int MaximumAttempts = 3, RetryDelayMs = 5, RetryBudgetMs = 20;
    private sealed record Decision(string Reason, bool Retryable = false, string? Field = null, int? IoHResult = null)
    { internal bool Idle => Reason == "idle"; }
    private sealed class SchemaFailure(string reason, string? fieldName = null) : Exception
    { internal string Reason => reason; internal string? Field => fieldName; }

    internal static string ReadFile(string path)
    {
        // Do not prevent the legacy writer from atomically replacing its snapshot.
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(file);
        return reader.ReadToEnd();
    }

    internal static LegacyStatusResult Check(LegacyStatusKind kind, Func<string> read,
        Func<long> monotonicMs, Func<DateTimeOffset> utcNow, Action<int> delay,
        Action<LegacyStatusObservation>? observe = null)
    {
        var prefix = kind == LegacyStatusKind.Receiver ? "legacy_receiver_" : "legacy_controller_";
        var began = monotonicMs();
        for (var attempt = 1; ; attempt++)
        {
            var decision = ReadDecision(kind, read, utcNow);
            var elapsed = Math.Max(0, monotonicMs() - began);
            var retry = decision.Retryable && attempt < MaximumAttempts && elapsed + RetryDelayMs < RetryBudgetMs;
            var code = prefix + decision.Reason;
            if (!decision.Idle || attempt > 1)
                observe?.Invoke(new(code, attempt, elapsed, retry, decision.Field, decision.IoHResult));
            if (!retry) return new(decision.Idle, code, attempt, elapsed);
            delay(RetryDelayMs);
            elapsed = Math.Max(0, monotonicMs() - began);
            if (elapsed >= RetryBudgetMs)
            {
                observe?.Invoke(new(code, attempt, elapsed, false, decision.Field, decision.IoHResult));
                return new(false, code, attempt, elapsed);
            }
        }
    }

    private static Decision ReadDecision(LegacyStatusKind kind, Func<string> read, Func<DateTimeOffset> utcNow)
    {
        string json;
        try { json = read(); }
        catch (IOException error)
        {
            var reason = error is FileNotFoundException or DirectoryNotFoundException ? "file_missing"
                : (error.HResult & 0xffff) is 32 or 33 ? "sharing_violation" : "io_error";
            return new(reason, true, IoHResult: error.HResult);
        }
        catch (UnauthorizedAccessException error) { return new("access_denied", IoHResult: error.HResult); }
        catch { return new("read_unknown"); }
        try
        {
            using var status = JsonDocument.Parse(json);
            if (status.RootElement.ValueKind != JsonValueKind.Object) return new("invalid_schema");
            return kind == LegacyStatusKind.Receiver ? Receiver(status.RootElement, utcNow()) : Controller(status.RootElement);
        }
        catch (JsonException) { return new(IncompleteJson(json) ? "json_incomplete" : "json_invalid", true); }
        catch (SchemaFailure error) { return new(error.Reason, Field: error.Field); }
        catch { return new("state_unknown"); }
    }
    private static bool IncompleteJson(string json)
    {
        try
        {
            var reader = new Utf8JsonReader(Encoding.UTF8.GetBytes(json), isFinalBlock: false, state: default);
            while (reader.Read()) { }
            return true;
        }
        catch (JsonException) { return false; }
    }
    private static JsonElement Property(JsonElement root, string name) => root.TryGetProperty(name, out var value)
        ? value : throw new SchemaFailure("missing_field", name);
    private static bool Boolean(JsonElement root, string name)
    {
        var value = Property(root, name);
        return value.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? value.GetBoolean() : throw new SchemaFailure("invalid_field", name);
    }
    private static string Text(JsonElement root, string name)
    {
        var value = Property(root, name);
        return value.ValueKind == JsonValueKind.String ? value.GetString()! : throw new SchemaFailure("invalid_field", name);
    }
    private static bool Session(JsonElement root, string name)
    {
        var value = Property(root, name);
        if (value.ValueKind == JsonValueKind.Null) return false;
        if (value.ValueKind == JsonValueKind.String && Guid.TryParse(value.GetString(), out _)) return true;
        throw new SchemaFailure("invalid_field", name);
    }
    private static Decision Receiver(JsonElement root, DateTimeOffset now)
    {
        if (Session(root, "session")) return new("busy");
        if (Session(root, "cancelled_session")) return new("cancelled_tail");
        if (!Boolean(root, "running")) return new("not_running");
        var timestamp = Property(root, "utc");
        if (timestamp.ValueKind != JsonValueKind.String || !timestamp.TryGetDateTimeOffset(out var written))
            throw new SchemaFailure("invalid_field", "utc");
        if (written > now) return new("future_timestamp");
        if (now - written > TimeSpan.FromSeconds(10)) return new("stale");
        return new("idle");
    }
    private static Decision Controller(JsonElement root)
    {
        switch (Text(root, "State"))
        {
            case "ready": return new("idle");
            case "waiting-for-next-down":
            case "round-completed":
                var data = Property(root, "Data");
                if (data.ValueKind != JsonValueKind.Object) throw new SchemaFailure("invalid_field", "Data");
                return new(Boolean(data, "ReadyForNext") ? "idle" : "not_ready");
            case "round-starting":
            case "rpc-call":
            case "rpc-return": return new("busy");
            case "listener-fault":
            case "timeout-unknown": return new("faulted");
            case "prepared":
            case "installer-context":
            case "listener-stopped": return new("not_ready");
            default: return new("state_unknown");
        }
    }
}
