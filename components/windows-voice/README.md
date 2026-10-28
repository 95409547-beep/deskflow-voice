<!-- SPDX-FileCopyrightText: 2026 Deskflow Voice contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# Windows voice component

This component curates the authored Windows prototype into an independently buildable .NET 10 project. It does not replace or configure an installed Deskflow/IME/voice deployment. Help and managed checks require no runtime configuration:

```powershell
dotnet run --project Deskflow.WindowsVoice.csproj -- --help
dotnet run --project Deskflow.WindowsVoice.csproj -- --check-managed
```

No arguments also prints help. Unknown arguments, including the prototype's live hook/RPC flags, return exit code 2. The explicit `--serve-v2 <private-config>` entry starts the [V2 TLS receiver](V2.md); native input begins only after an authenticated Begin. It uses the installed IME compatibility adapter directly, with no synthetic keys, new keyboard hook, or application whitelist. The component does not install drivers, configure startup, select default devices, change mute/volume, or read dictation text.

## Build and self-contained verification

From PowerShell 7 with the .NET 10 SDK:

```powershell
./build.ps1
./build.ps1 -Publish
```

The second command produces a self-contained win-x64 executable under ignored `artifacts/publish/win-x64/` and runs only its managed checks. This is a local verification output, not an installer. No third-party runtime binary is committed. Temporary files, SDK home and package restore caches stay under ignored `artifacts/`; the entry suppresses first-run development-certificate generation. The self-contained profile disables trimming because native interop/delegate discovery is part of the future runtime adapter.

Managed checks cover serial ownership/cleanup with fake RPCs, foreground identity snapshots, window switching and cancelled-session tails, simulated 250ms output-queue backpressure, frame bounds, token comparison, configuration validation, and safe CLI dispatch. They do not query windows, activate audio, install a hook, load the vendor DLL, connect a pipe/listener, or read an installed runtime configuration.

V2 checks add epoch isolation, decimal sequence validation, repeated sessions, acknowledgement ordering, cancellation, audio limits, timeout cleanup and failed-start/end handling. These use a fake backend and clock. Passing them does not establish live IME or cross-device acceptance.

## Reusable source

- `src/Receiver.cs`: bounded TLS/JSON framing, authenticated audio sessions, 48k mono PCM to configured stereo output, per-BEGIN foreground identity and key lifetime, timeout/disconnect cleanup. The bootstrap server and fixed-editor path were removed.
- `src/V2Protocol.cs`, `V2Server.cs`, `V2NativeBackend.cs`, `V2WaveOutput.cs`: separate V2 protocol, explicit server entry, direct installed-IME capture control, and bounded output with strict tail drain. The original receiver file is unchanged by V2.
- `src/RepeatSession.cs`, `ForegroundTargets.cs`, `HotkeyPattern.cs`: managed session/gate logic and metadata-based gesture policy. An injected event pattern is not a claim about injector PID.
- `src/HotkeyRepeat.cs`: serial foreground controller, metadata-only hook callback, per-call deadline and safe stop semantics. Its runtime entry is not exposed by this executable.
- `src/CurrentVoiceService.cs`, `NativePlatform.cs`: least-access process identity, next-start service binding, explicit installation verification, and the existing installed-IME compatibility ABI. Only FocusIn and SimpleMessage are bound; there are no transcript/sink/read/commit calls. This is an unofficial adapter contract, not a vendor API guarantee.
- `src/Configuration.cs`: installation/state paths derive from explicit configuration or Windows ProgramFiles/LocalApplicationData. Service identity is discovered each startup and fixed within a session. File hashes and signer identity must be supplied by installation verification, not copied from a development machine.

`receiver.example.json` documents the earlier library settings; `receiver-v2.example.json` describes the V2 entry. Both are non-operational loopback examples. Pairing credentials and certificate passwords are environment references, not values in a settings file. Relative paths resolve against the configuration file. An explicit unique audio-output prefix is required; no endpoint instance GUID is embedded. User-logon supervision and provisioning remain outside this component.

## Scope and provenance

See [PROVENANCE.md](PROVENANCE.md) for the authored prototype inputs and curation. All added source is licensed under `GPL-2.0-only WITH LicenseRef-OpenSSL-Exception`, using the repository's existing license texts. Windows API declarations and a vendor ABI contract are authored interop definitions; no Windows SDK header, vendor source, native binary, driver, certificate, private settings, runtime manifest or operational log is copied.

The prototype's firewall/UAC/comparison tools, fixed Notepad/old PID gates, default preflight launcher, startup manager, microphone-selection helpers and one-time mute repair are not included. This component currently contains no Windows audio sender, Qt product integration or installed-runtime acceptance result. Windows-to-Windows live acceptance and product activation belong to subsequent stages.
