<!-- SPDX-FileCopyrightText: 2026 Deskflow Voice contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# M1 validation record

Validated on 2026-10-27 with .NET SDK 10.0.400 and PowerShell 7.6.5, on Windows x64. These are development verification results, not installed-runtime acceptance.

| Check | Result |
|---|---|
| Release build | Passed, zero warnings and errors |
| Self-contained single-file win-x64 publish | Passed |
| Published executable, no arguments | Help, exit 0 |
| Prototype live-hook argument | Rejected, exit 2 |
| Published managed checks | 82 passed |
| Native vendor RPC / hook / key / audio / listener activation | None |
| Existing runtime, startup, services, drivers | Not modified or restarted |

The checks comprise 35 repeat/foreground controller assertions, 15 foreground receiver assertions, 8 simulated queue assertions and 24 CLI/configuration/framing/pattern assertions. Older one-shot/editor-specific/operator checks are not imported. Temporary generated silence is used only by the in-memory fake queue; no audio device is opened and no samples are saved.

Source screening found no actual user/workspace path, development LAN address, endpoint instance identifier, historical process/window identifier, embedded credential candidate, private certificate/key, runtime manifest, operational log, third-party source/binary, or imported operator script. All component files have the selected SPDX header or a JSON license sidecar. The only literal GUID is the standard Windows WinTrust action identifier, not an audio endpoint ID. Authored input provenance and original hashes are documented separately.

The native adapters are compiled but intentionally not exercised against an installed IME in M1. Qt lifecycle integration, pairing/storage, a Windows sender, live interoperability on the second Windows machine and any installer/distribution validation remain later work.
