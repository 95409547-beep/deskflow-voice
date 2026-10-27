<!-- SPDX-FileCopyrightText: 2026 Deskflow Voice contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# Source provenance and license

All new files in this component are authored Deskflow Voice integration work, licensed under `GPL-2.0-only WITH LicenseRef-OpenSSL-Exception` as selected for the product. Applicable texts already exist at the repository root in `LICENSES/`. No third-party source or executable/library was imported or relicensed.

The input was the locally archived Windows prototype, authored during the October 2026 voice-integration sessions. The raw archive remains an internal snapshot; it is not copied wholesale into this repository. The table hashes identify the original input bytes before adaptation, not the current modified source.

| Component file | Original authored file | Original SHA256 |
|---|---|---|
| `src/Receiver.cs` | `bridge/Receiver.cs` | `8B5F12503FA2CFAF4639E2602E1C78E1A4449199474F8FBB9E5BB1956B8DDA98` |
| `src/RepeatSession.cs` | `bridge/rpc-probe/RepeatSession.cs` | `18C528A0629675F847D79C9F60888254C0C53C255897AA8F4BFAEC2793BDF801` |
| `src/ForegroundTargets.cs` | `bridge/rpc-probe/ForegroundTargets.cs` | `F227633D7B739D994C27778DF32E88CCD46D8C9BCD0A6FEE8647B5B443DD5E7E` |
| `src/HotkeyRepeat.cs` | `bridge/rpc-probe/HotkeyRepeat.cs` | `22AB72C5CEFE228436F593CC5B25FAF5E36910C95512F8F5B7E22C10F576A042` |
| `checks/RepeatChecks.cs` | `bridge/rpc-probe/RepeatChecks.cs` | `9C61C509DEAD2181D441E18281F3B9CA33BDEAA286C4D4AEA2A5CB13E9AD42DC` |
| `checks/ForegroundModeChecks.cs` | `bridge/rpc-probe/ForegroundModeChecks.cs` | `F614A96738BA2B720896988730FF1C7506DF7E710C2C5FA9952F218BDB20DE42` |
| `checks/ForegroundReceiverChecks.cs` | `bridge/ForegroundReceiverChecks.cs` | `4A70CAD3AD8A3C458E92A86DDC99B927888B420365E5FB2F0969A55A529B70E4` |
| `checks/WaveQueueChecks.cs` | `bridge/WaveQueueChecks.cs` | `B5FDC51B3575846DCB4B31C893C50EE54651C7968CA0F1DE58557F7E74E2456C` |
`HotkeyPattern.cs` extracts metadata helpers from the authored `HotkeyOnce.cs`; its legacy listener and editor allowlist were omitted. `NativePlatform.cs` and `CurrentVoiceService.cs` replace the prototype's machine-pinned `NativePreparation.cs`/service helper with configured installation discovery, dynamic process identity, SHA256 pins and Windows Authenticode verification. These include authored declarations of Windows API contracts and the installed vendor's compatibility exports; they do not incorporate Windows SDK header bodies or vendor implementation code. Configuration, CLI dispatch, the additional managed checks, project/publish profile and build entry are new M1 code.

Curation removed actual user/workspace paths, LAN addresses, endpoint instance IDs, historical process/window IDs, version-specific installed paths, raw manifests and private configuration. Windows API/action GUIDs and synthetic test identities are retained where required. The receiver is foreground-only; bootstrap provisioning and fixed-editor control were removed. The executable exposes only help and managed checks.

Excluded operator code includes firewall provisioning, elevation/UAC comparison experiments, startup/service/task management, microphone selection, endpoint mute repair, default native preflight and one-shot trials. Certificates, credentials, vendor DLLs, drivers, runtime logs, captured content and generated binaries are excluded.

The native adapters compile but have not been activated against an installed IME by this import. The acceptance result here concerns build, managed state/protocol checks and safe command dispatch. Existing live deployment acceptance is separate from this component's future integration/installation tests.

The curated input was reviewed from source commit `c8b7f77fd53c65e28add16d928ee3773deb1a60e` in the Windows preparation checkout. The verified source-only transfer archive has SHA256 `8c1814439649660047a6e384d8b1f3a399e60b1d1ef443843ad2783b96464831`. This records provenance; the archive itself remains internal. Product integration adds the SDK pin, root CI entry and build working-directory selection after this snapshot.
