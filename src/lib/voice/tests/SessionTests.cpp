/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "voice/Session.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace deskflow::voice;

static void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

static void expect(const std::vector<Effect> &effects, std::initializer_list<EffectKind> kinds)
{
  require(effects.size() == kinds.size(), "unexpected effect count");
  auto actual = effects.begin();
  for (auto kind : kinds)
    require((actual++)->kind == kind, "unexpected effect kind/order");
}

static Route target()
{
  return {"trusted-peer", 1, Platform::Windows, true, true};
}

static void repeatAndLateReplies(Platform sender)
{
  Session s(sender);
  s.routeChanged(target());
  for (std::uint64_t n = 1; n <= 20; ++n) {
    expect(s.press(n), {EffectKind::StartCapture, EffectKind::Begin});
    expect(s.press(n), {});
    expect(s.receiverStarted(n - 1), {});
    expect(s.receiverStarted(n), {EffectKind::ForwardAudio});
    expect(s.receiverStarted(n), {});
    expect(s.release(), {EffectKind::StopCapture, EffectKind::End});
    expect(s.release(), {});
    s.receiverEnded(n - 1);
    require(s.phase() == Phase::Stopping, "stale END completed a new round");
    s.receiverEnded(n);
    require(s.phase() == Phase::Idle && !s.capturing(), "round not stopped");
  }
  expect(s.press(20), {});
  require(!s.capturing(), "replayed sequence captured audio");
}

static void releaseBeforeStartAndRouteChange()
{
  Session s(Platform::Windows);
  s.routeChanged(target());
  s.press(1);
  expect(s.release(), {EffectKind::StopCapture, EffectKind::End});
  expect(s.receiverStarted(1), {});
  s.receiverEnded(1);
  s.press(2);
  s.receiverStarted(2);
  auto next = target();
  next.generation = 2;
  auto effects = s.routeChanged(next);
  expect(effects, {EffectKind::StopCapture, EffectKind::End});
  require(effects.back().target == target(), "END moved to the new target");
  s.receiverEnded(2);
  require(s.phase() == Phase::ReleaseRequired, "held key rearmed after target change");
  expect(s.press(3), {});
  s.release();
  auto fresh = s.press(3);
  require(fresh.back().target == next, "fresh gesture did not bind new target");
}

static void disconnectAndDeadline()
{
  Session s(Platform::MacOS);
  s.routeChanged(target());
  s.press(1);
  expect(s.connectionLost(), {EffectKind::StopCapture});
  expect(s.receiverStarted(1), {});
  s.routeChanged(target());
  expect(s.press(2), {});
  s.release();
  s.press(2);
  expect(s.deadlineExpired(1), {});
  expect(s.deadlineExpired(2), {EffectKind::StopCapture, EffectKind::End});
  expect(s.receiverStarted(2), {});
  s.release();
  expect(s.press(3), {});
  require(!s.capturing(), "timeout reused stale receiver readiness");
  s.release();
  s.routeChanged(target());
  expect(s.press(3), {EffectKind::StartCapture, EffectKind::Begin});
}

static void capabilitiesAndUnavailableRoute()
{
  for (auto receiver : {Platform::MacOS, Platform::Unsupported}) {
    Session s(Platform::Windows);
    auto r = target();
    r.receiver = receiver;
    s.routeChanged(r);
    expect(s.press(1), {});
    require(!s.capturing(), "unsupported V1 receiver accepted");
  }
  Session unsupported(Platform::Unsupported);
  unsupported.routeChanged(target());
  expect(unsupported.press(1), {});
  for (int unavailable = 0; unavailable < 4; ++unavailable) {
    Session s(Platform::MacOS);
    auto r = target();
    if (unavailable == 0)
      r.authenticated = false;
    if (unavailable == 1)
      r.ready = false;
    if (unavailable == 2)
      r.peerIdentity.clear();
    if (unavailable == 3)
      r.generation = 0;
    s.routeChanged(r);
    expect(s.press(1), {});
    s.routeChanged(target());
    expect(s.press(2), {});
    s.release();
    expect(s.press(2), {EffectKind::StartCapture, EffectKind::Begin});
  }
}

static void receiverStopsWhileHeld()
{
  Session s(Platform::Windows);
  s.routeChanged(target());
  s.press(1);
  s.receiverStarted(1);
  expect(s.receiverEnded(1), {EffectKind::StopCapture});
  require(!s.capturing() && s.phase() == Phase::ReleaseRequired, "receiver stop left local audio recording");
  expect(s.receiverStarted(1), {});
  expect(s.press(2), {});
  s.release();
  expect(s.press(2), {EffectKind::StartCapture, EffectKind::Begin});
}

int main()
{
  try {
    repeatAndLateReplies(Platform::MacOS);
    repeatAndLateReplies(Platform::Windows);
    releaseBeforeStartAndRouteChange();
    disconnectAndDeadline();
    capabilitiesAndUnavailableRoute();
    receiverStopsWhileHeld();
    std::cout << "PASS: both senders, 40 repeated rounds, release-before-start, focus, disconnect, timeout and V1 "
                 "capabilities\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
