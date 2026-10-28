/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace deskflow::voice {
enum class HoldKey
{
  Other,
  F8,
  RightOption,
  RightControl
};
enum class KeyEventKind
{
  State,
  Down,
  Up,
  Cancel
};
struct ScreenContext
{
  std::string name;
  bool remote = false, eligible = false;
  bool operator==(const ScreenContext &) const = default;
};
struct KeyGateEvent
{
  KeyEventKind kind;
  std::uint64_t serial, generation, hold;
  std::string lease;
  ScreenContext screen;
  bool held, enabled;
};
struct KeyDecision
{
  bool consume = false;
  std::vector<KeyGateEvent> events;
};
// Pure, bounded-lifetime interception policy. Callers translate key identities,
// relay only the returned decision, and deliver events without native I/O here.
class KeyGate
{
public:
  static constexpr std::int64_t leaseMs = 5000;
  std::vector<KeyGateEvent> context(ScreenContext value);
  std::vector<KeyGateEvent>
  configure(std::string lease, std::string target, HoldKey key, bool enabled, std::int64_t now);
  std::vector<KeyGateEvent> disarm();
  bool renew(const std::string &lease, std::int64_t now);
  std::vector<KeyGateEvent> tick(std::int64_t now);
  KeyGateEvent snapshot();
  KeyDecision down(HoldKey key, std::uint32_t button, bool modifiersAllowed, std::int64_t now);
  KeyDecision up(std::uint32_t button, std::int64_t now);
  bool repeat(std::uint32_t button) const;
  HoldKey suppressedModifier() const;

private:
  KeyGateEvent event(KeyEventKind kind);
  void cancel(std::vector<KeyGateEvent> &events);
  void expire(std::int64_t now, std::vector<KeyGateEvent> &events);
  mutable std::mutex m_mutex;
  ScreenContext m_context;
  std::string m_lease, m_target, m_heldLease;
  HoldKey m_key = HoldKey::Other, m_heldKey = HoldKey::Other;
  bool m_enabled = false, m_sentDown = false;
  std::uint32_t m_button = 0;
  std::set<std::uint32_t> m_passthrough;
  std::uint64_t m_serial = 0, m_generation = 1, m_hold = 0, m_heldGeneration = 0;
  std::int64_t m_renewed = 0;
};
} // namespace deskflow::voice
