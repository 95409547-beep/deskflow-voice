/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "Session.h"

#include <utility>

namespace deskflow::voice {

bool Route::canReceive() const
{
  return receiver == Platform::Windows && authenticated && ready && !peerIdentity.empty() && !inputContext.empty();
}

Session::Session(Platform sender) : m_sender(sender)
{
}

std::vector<Effect> Session::routeChanged(Route route)
{
  const bool changed = route != m_route;
  m_route = std::move(route);
  if (changed && m_target)
    return stopOwnedRound(true);
  return {};
}

std::vector<Effect> Session::press(std::uint64_t sequence)
{
  if (m_pressed)
    return {};
  m_pressed = true;
  if (m_phase != Phase::Idle)
    return {};
  if ((m_sender != Platform::MacOS && m_sender != Platform::Windows) || !m_route.canReceive() || sequence == 0 ||
      sequence <= m_lastSequence) {
    m_phase = Phase::ReleaseRequired;
    return {};
  }
  m_lastSequence = m_sequence = sequence;
  m_target = m_route;
  m_phase = Phase::Starting;
  m_capturing = true;
  // Capture only after the real DOWN. Buffer a bounded preroll locally until
  // the receiver acknowledges its owned native recording has actually started.
  return {{EffectKind::StartCapture, sequence, *m_target}, {EffectKind::Begin, sequence, *m_target}};
}

std::vector<Effect> Session::receiverStarted(std::uint64_t sequence)
{
  if (!owns(sequence) || m_phase != Phase::Starting || !m_pressed)
    return {};
  m_phase = Phase::Recording;
  return {{EffectKind::ForwardAudio, sequence, *m_target}};
}

std::vector<Effect> Session::release()
{
  m_pressed = false;
  if (m_phase == Phase::ReleaseRequired) {
    m_phase = Phase::Idle;
    return {};
  }
  return stopOwnedRound(true);
}

std::vector<Effect> Session::stopOwnedRound(bool transportAvailable)
{
  if (!m_target || m_phase == Phase::Stopping)
    return {};
  std::vector<Effect> effects;
  if (m_capturing) {
    effects.push_back({EffectKind::StopCapture, m_sequence, *m_target});
    m_capturing = false;
  }
  if (transportAvailable)
    effects.push_back({EffectKind::End, m_sequence, *m_target});
  m_phase = Phase::Stopping;
  return effects;
}

std::vector<Effect> Session::receiverEnded(std::uint64_t sequence)
{
  if (!owns(sequence))
    return {};
  // The receiver may stop first (for example after its focus changes). Stop
  // local capture too, and require release before another physical gesture.
  auto effects = stopOwnedRound(false);
  resetRound();
  return effects;
}

std::vector<Effect> Session::connectionLost()
{
  auto effects = stopOwnedRound(false);
  m_route.ready = false;
  resetRound();
  return effects;
}

std::vector<Effect> Session::deadlineExpired(std::uint64_t sequence)
{
  if (!owns(sequence))
    return {};
  auto effects = stopOwnedRound(true);
  // Unknown native state requires a fresh readiness check and a new physical
  // press. A late acknowledgment cannot resume the interrupted recording.
  m_route.ready = false;
  resetRound();
  return effects;
}

bool Session::owns(std::uint64_t sequence) const
{
  return m_target && sequence == m_sequence;
}

void Session::resetRound()
{
  m_target.reset();
  m_sequence = 0;
  m_capturing = false;
  m_phase = m_pressed ? Phase::ReleaseRequired : Phase::Idle;
}

} // namespace deskflow::voice
