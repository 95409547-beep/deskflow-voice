/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "KeyGate.h"
#include <utility>

namespace deskflow::voice {
KeyGateEvent KeyGate::event(KeyEventKind kind)
{
  const bool keyEvent = kind != KeyEventKind::State;
  return {
      kind,
      ++m_serial,
      keyEvent ? m_heldGeneration : m_generation,
      m_hold,
      keyEvent ? m_heldLease : m_lease,
      m_context,
      m_button != 0,
      m_enabled
  };
}
void KeyGate::cancel(std::vector<KeyGateEvent> &events)
{
  if (m_sentDown) {
    events.push_back(event(KeyEventKind::Cancel));
    m_sentDown = false;
  }
}
std::vector<KeyGateEvent> KeyGate::context(ScreenContext value)
{
  const std::lock_guard lock(m_mutex);
  if (value == m_context)
    return {};
  std::vector<KeyGateEvent> events;
  cancel(events);
  m_context = std::move(value);
  ++m_generation;
  if (m_context.name.empty())
    m_passthrough.clear();
  events.push_back(event(KeyEventKind::State));
  return events;
}
std::vector<KeyGateEvent>
KeyGate::configure(std::string lease, std::string target, HoldKey key, bool enabled, std::int64_t now)
{
  const std::lock_guard lock(m_mutex);
  std::vector<KeyGateEvent> events;
  enabled = enabled && !lease.empty() && !target.empty() && key != HoldKey::Other;
  if (m_lease == lease && m_target == target && m_key == key && m_enabled == enabled) {
    m_renewed = now;
    return {};
  }
  cancel(events);
  m_lease = std::move(lease);
  m_target = std::move(target);
  m_key = key;
  m_enabled = enabled;
  m_renewed = now;
  ++m_generation;
  events.push_back(event(KeyEventKind::State));
  return events;
}
std::vector<KeyGateEvent> KeyGate::disarm()
{
  const std::lock_guard lock(m_mutex);
  std::vector<KeyGateEvent> events;
  cancel(events);
  m_enabled = false;
  m_lease.clear();
  ++m_generation;
  events.push_back(event(KeyEventKind::State));
  return events;
}
bool KeyGate::renew(const std::string &lease, std::int64_t now)
{
  const std::lock_guard lock(m_mutex);
  if (!m_enabled || lease != m_lease)
    return false;
  m_renewed = now;
  return true;
}
void KeyGate::expire(std::int64_t now, std::vector<KeyGateEvent> &events)
{
  if (m_enabled && (now < m_renewed || now - m_renewed >= leaseMs)) {
    cancel(events);
    m_enabled = false;
    ++m_generation;
    events.push_back(event(KeyEventKind::State));
  }
}
std::vector<KeyGateEvent> KeyGate::tick(std::int64_t now)
{
  const std::lock_guard lock(m_mutex);
  std::vector<KeyGateEvent> events;
  expire(now, events);
  return events;
}
KeyGateEvent KeyGate::snapshot()
{
  const std::lock_guard lock(m_mutex);
  return event(KeyEventKind::State);
}
KeyDecision KeyGate::down(HoldKey key, std::uint32_t button, bool modifiersAllowed, std::int64_t now)
{
  const std::lock_guard lock(m_mutex);
  KeyDecision result;
  expire(now, result.events);
  if (!button)
    return result;
  if (button == m_button) {
    result.consume = true;
    return result;
  }
  if (m_passthrough.contains(button))
    return result;
  if (!m_button && m_passthrough.empty() && modifiersAllowed && m_enabled && key == m_key && m_context.remote &&
      m_context.eligible && m_context.name == m_target) {
    m_button = button;
    m_heldKey = key;
    m_sentDown = true;
    m_heldLease = m_lease;
    m_heldGeneration = m_generation;
    ++m_hold;
    result.consume = true;
    result.events.push_back(event(KeyEventKind::Down));
  } else if (m_passthrough.size() < 512)
    m_passthrough.insert(button);
  return result;
}
KeyDecision KeyGate::up(std::uint32_t button, std::int64_t now)
{
  const std::lock_guard lock(m_mutex);
  KeyDecision result;
  expire(now, result.events);
  if (button && button == m_button) {
    result.consume = true;
    result.events.push_back(event(KeyEventKind::Up));
    m_button = 0;
    m_sentDown = false;
    m_heldKey = HoldKey::Other;
    result.events.push_back(event(KeyEventKind::State));
  } else
    m_passthrough.erase(button);
  return result;
}
bool KeyGate::repeat(std::uint32_t button) const
{
  const std::lock_guard lock(m_mutex);
  return button && button == m_button;
}
HoldKey KeyGate::suppressedModifier() const
{
  const std::lock_guard lock(m_mutex);
  return m_button ? m_heldKey : HoldKey::Other;
}
} // namespace deskflow::voice
