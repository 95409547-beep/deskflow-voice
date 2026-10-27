/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deskflow::voice {

enum class Platform
{
  MacOS,
  Windows,
  Unsupported
};
enum class Phase
{
  Idle,
  Starting,
  Recording,
  Stopping,
  ReleaseRequired
};
enum class EffectKind
{
  StartCapture,
  Begin,
  ForwardAudio,
  StopCapture,
  End
};

struct Route
{
  std::string peerIdentity;
  std::string inputContext;
  Platform receiver = Platform::Unsupported;
  bool authenticated = false;
  bool ready = false;

  bool operator==(const Route &) const = default;
  bool canReceive() const;
};

struct Effect
{
  EffectKind kind;
  std::uint64_t session;
  Route target;
};

// Deterministic sender lifecycle. Effects are instructions, not native calls.
// Platform adapters validate context identities and execute effects separately.
// A new connection owns a fresh instance; messages carry its connection epoch.
class Session
{
public:
  explicit Session(Platform sender);
  std::vector<Effect> routeChanged(Route route);
  std::vector<Effect> press(std::uint64_t sequence);
  std::vector<Effect> release();
  std::vector<Effect> receiverStarted(std::uint64_t sequence);
  std::vector<Effect> receiverEnded(std::uint64_t sequence);
  std::vector<Effect> connectionLost();
  std::vector<Effect> deadlineExpired(std::uint64_t sequence);

  Phase phase() const
  {
    return m_phase;
  }
  bool capturing() const
  {
    return m_capturing;
  }
  bool pressed() const
  {
    return m_pressed;
  }

private:
  std::vector<Effect> stopOwnedRound(bool transportAvailable);
  void resetRound();
  bool owns(std::uint64_t sequence) const;

  Platform m_sender;
  Route m_route;
  std::optional<Route> m_target;
  std::uint64_t m_sequence = 0;
  std::uint64_t m_lastSequence = 0;
  Phase m_phase = Phase::Idle;
  bool m_pressed = false;
  bool m_capturing = false;
};

} // namespace deskflow::voice
