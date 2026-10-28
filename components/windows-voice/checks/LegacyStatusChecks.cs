// SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
using System.Text.Json;

namespace DeskflowVoice.Windows;

internal static class LegacyStatusChecks
{
    private static readonly DateTimeOffset Utc = new(2030, 1, 1, 0, 0, 0, TimeSpan.Zero);
    private static string Receiver(string? session = null, string? cancelled = null, int ageSeconds = 0) =>
        JsonSerializer.Serialize(new { running = true, session, cancelled_session = cancelled, utc = Utc.AddSeconds(-ageSeconds) });
    private static string Controller(string state, bool ready = true) =>
        JsonSerializer.Serialize(new { State = state, Data = new { ReadyForNext = ready } });
    private static IOException Sharing() => new("PRIVATE_FIXTURE_TEXT", unchecked((int)0x80070020));
    private sealed class Fixture(params object[] files)
    {
        private readonly Queue<object> files = new(files);
        private object last = Receiver();
        internal long Now;
        internal int Reads, ReadCost, DelayJump;
        internal readonly List<int> Delays = new();
        internal readonly List<LegacyStatusObservation> Observations = new();
        private string Read()
        {
            Reads++; Now += ReadCost;
            if (files.Count > 0) last = files.Dequeue();
            if (last is Exception error) throw error;
            return (string)last;
        }
        internal LegacyStatusResult Check(LegacyStatusKind kind = LegacyStatusKind.Receiver) => LegacyStatus.Check(kind,
            Read, () => Now, () => Utc.AddMilliseconds(Now), milliseconds =>
            { Delays.Add(milliseconds); Now += DelayJump > 0 ? DelayJump : milliseconds; }, Observations.Add);
    }
    internal static Dictionary<string, bool> Run()
    {
        var checks = new Dictionary<string, bool>();
        void Check(string name, bool passed)
        {
            checks.Add("legacy:" + name, passed);
            if (!passed) throw new InvalidOperationException("Legacy status check failed: " + name);
        }
        static bool Is(LegacyStatusResult result, string reason) => !result.Idle && result.Code == "legacy_receiver_" + reason;
        var idle = new Fixture(Receiver());
        Check("fresh-receiver-idle-without-delay", idle.Check().Idle && idle.Reads == 1 && idle.Delays.Count == 0 && idle.Observations.Count == 0);
        var busy = new Fixture(Receiver(Guid.NewGuid().ToString()), Receiver());
        Check("real-busy-immediate-never-retried-into-idle", Is(busy.Check(), "busy") && busy.Reads == 1 && busy.Delays.Count == 0);
        var cancelled = new Fixture(Receiver(cancelled: Guid.NewGuid().ToString()), Receiver());
        Check("cancelled-tail-is-distinct-and-immediate", Is(cancelled.Check(), "cancelled_tail") && cancelled.Reads == 1);
        var stopped = new Fixture("{\"running\":false,\"session\":null,\"cancelled_session\":null}");
        Check("not-running-fails-closed", Is(stopped.Check(), "not_running") && stopped.Delays.Count == 0);
        var stale = new Fixture(Receiver(ageSeconds: 11), Receiver());
        Check("stale-heartbeat-not-retried-or-accepted", Is(stale.Check(), "stale") && stale.Reads == 1);
        Check("ten-second-heartbeat-boundary-preserved", new Fixture(Receiver(ageSeconds: 10)).Check().Idle);
        Check("future-heartbeat-fails-closed", Is(new Fixture(Receiver(ageSeconds: -1)).Check(), "future_timestamp"));
        var shared = new Fixture(Sharing(), Receiver()); var recovered = shared.Check();
        Check("sharing-then-fresh-snapshot-recovers", recovered.Idle && recovered.Attempts == 2 && shared.Delays.SequenceEqual(new[] { 5 }));
        Check("sharing-diagnostic-not-reported-as-real-busy", shared.Observations[0].Code == "legacy_receiver_sharing_violation" && shared.Observations[0].Retrying && shared.Observations[1].Code == "legacy_receiver_idle");
        Check("diagnostics-contain-no-file-or-exception-content", !JsonSerializer.Serialize(shared.Observations).Contains("PRIVATE_FIXTURE_TEXT", StringComparison.Ordinal));
        var io = new Fixture(new IOException(), new IOException(), Receiver());
        Check("generic-io-can-recover-on-third-read", io.Check().Idle && io.Reads == 3 && io.Now == 10 && io.Observations[0].Code == "legacy_receiver_io_error");
        var missing = new Fixture(new FileNotFoundException(), Receiver());
        Check("atomic-replacement-missing-file-gap-can-recover", missing.Check().Idle && missing.Reads == 2 && missing.Observations[0].Code == "legacy_receiver_file_missing");
        var incomplete = new Fixture("{\"running\":", Receiver());
        Check("partial-json-can-recover-with-distinct-reason", incomplete.Check().Idle && incomplete.Observations[0].Code == "legacy_receiver_json_incomplete");
        var malformed = new Fixture("{invalid}", Receiver());
        Check("malformed-json-retry-is-bounded-and-labelled", malformed.Check().Idle && malformed.Observations[0].Code == "legacy_receiver_json_invalid");
        var missingField = new Fixture("{\"running\":true,\"session\":null,\"cancelled_session\":null}", Receiver());
        Check("missing-field-immediate-unknown-rejection", Is(missingField.Check(), "missing_field") && missingField.Reads == 1 && missingField.Observations[0].Field == "utc");
        Check("wrong-field-type-fails-closed", Is(new Fixture("{\"running\":true,\"session\":42}").Check(), "invalid_field"));
        Check("non-object-status-fails-closed", Is(new Fixture("[]").Check(), "invalid_schema"));
        var denied = new Fixture(new UnauthorizedAccessException(), Receiver());
        Check("access-denied-is-not-retried-as-transient-io", Is(denied.Check(), "access_denied") && denied.Reads == 1);
        var unknown = new Fixture(new InvalidOperationException(), Receiver());
        Check("unknown-read-failure-not-mislabelled-busy", Is(unknown.Check(), "read_unknown") && unknown.Reads == 1);
        var permanent = new Fixture(Sharing()); var rejected = permanent.Check();
        Check("persistent-sharing-fails-closed-after-three-reads", Is(rejected, "sharing_violation") && rejected.Attempts == 3 && permanent.Now == 10 && !permanent.Observations[^1].Retrying);
        var broken = new Fixture("{");
        Check("persistent-incomplete-json-fails-closed", Is(broken.Check(), "json_incomplete") && broken.Reads == 3 && broken.Now == 10);
        var slow = new Fixture(Sharing(), Receiver()) { ReadCost = 21 };
        Check("slow-read-prevents-further-retry-after-budget", Is(slow.Check(), "sharing_violation") && slow.Reads == 1 && slow.Delays.Count == 0);
        var jumped = new Fixture(Sharing(), Receiver()) { DelayJump = 25 };
        Check("clock-jump-during-delay-prevents-next-read", Is(jumped.Check(), "sharing_violation") && jumped.Reads == 1 && !jumped.Observations[^1].Retrying);
        var becameBusy = new Fixture(Sharing(), Receiver(Guid.NewGuid().ToString()), Receiver());
        Check("retry-that-observes-real-busy-stops-immediately", Is(becameBusy.Check(), "busy") && becameBusy.Reads == 2 && becameBusy.Delays.Count == 1);
        Check("controller-ready-event-snapshot-compatible", new Fixture("{\"State\":\"ready\"}").Check(LegacyStatusKind.Controller).Idle);
        Check("controller-waiting-with-explicit-ready-is-idle", new Fixture(Controller("waiting-for-next-down")).Check(LegacyStatusKind.Controller).Idle);
        var notReady = new Fixture(Controller("round-completed", false), Controller("ready"));
        Check("controller-not-ready-immediate-rejection", notReady.Check(LegacyStatusKind.Controller).Code == "legacy_controller_not_ready" && notReady.Reads == 1);
        var controllerBusy = new Fixture(Controller("rpc-call"), Controller("ready"));
        Check("controller-real-rpc-activity-immediately-busy", controllerBusy.Check(LegacyStatusKind.Controller).Code == "legacy_controller_busy" && controllerBusy.Reads == 1);
        var unknownState = new Fixture(Controller("unrecognized"));
        Check("unknown-controller-state-is-not-real-busy", unknownState.Check(LegacyStatusKind.Controller).Code == "legacy_controller_state_unknown" && unknownState.Reads == 1);
        Check("controller-fault-is-distinct", new Fixture(Controller("listener-fault")).Check(LegacyStatusKind.Controller).Code == "legacy_controller_faulted");
        Check("controller-missing-ready-field-fails-closed", new Fixture("{\"State\":\"waiting-for-next-down\",\"Data\":{}}").Check(LegacyStatusKind.Controller).Code == "legacy_controller_missing_field");
        var changing = new Fixture(Receiver(), Receiver(Guid.NewGuid().ToString()));
        Check("no-previous-idle-cache-used-on-next-check", changing.Check().Idle && Is(changing.Check(), "busy") && changing.Reads == 2);
        return checks;
    }
}
