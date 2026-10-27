/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXKeyStateTests.h"

#include "base/EventQueue.h"
#include <IOKit/hidsystem/IOLLEvent.h>
#include <QSignalSpy>
#include <QThread>

namespace {
class RecordingOSXKeyState : public OSXKeyState
{
public:
  using OSXKeyState::OSXKeyState;
  struct RecordedKey
  {
    KeyID id;
    KeyButton button;
    bool press;
    KeyModifierMask mask;
  };
  std::vector<RecordedKey> recorded;

  void sendKeyEvent(
      void *target, bool press, bool repeat, KeyID key, KeyModifierMask mask, int32_t count, KeyButton button
  ) override
  {
    recorded.push_back({key, button, press, mask});
    OSXKeyState::sendKeyEvent(target, press, repeat, key, mask, count, button);
  }
};
constexpr CGEventFlags leftOptionFlags = kCGEventFlagMaskAlternate | NX_DEVICELALTKEYMASK;
constexpr CGEventFlags rightOptionFlags = kCGEventFlagMaskAlternate | NX_DEVICERALTKEYMASK;
} // namespace

#define SHIFT_ID_L kKeyShift_L
#define SHIFT_ID_R kKeyShift_R
#define SHIFT_BUTTON 57
#define A_CHAR_ID 0x00000061
#define A_CHAR_BUTTON 001

void OSXKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void OSXKeyStateTests::mapModifiersFromOSX_OSXMask()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  KeyModifierMask outMask = 0;

  uint32_t shiftMask = 0 | kCGEventFlagMaskShift;
  outMask = keyState.mapModifiersFromOSX(shiftMask);
  QCOMPARE(outMask, KeyModifierShift);

  uint32_t ctrlMask = 0 | kCGEventFlagMaskControl;
  outMask = keyState.mapModifiersFromOSX(ctrlMask);
  QCOMPARE(outMask, KeyModifierControl);

  uint32_t altMask = 0 | kCGEventFlagMaskAlternate;
  outMask = keyState.mapModifiersFromOSX(altMask);
  QCOMPARE(outMask, KeyModifierAlt);

  uint32_t cmdMask = 0 | kCGEventFlagMaskCommand;
  outMask = keyState.mapModifiersFromOSX(cmdMask);
  QCOMPARE(outMask, KeyModifierSuper);

  uint32_t capsMask = 0 | kCGEventFlagMaskAlphaShift;
  outMask = keyState.mapModifiersFromOSX(capsMask);
  QCOMPARE(outMask, KeyModifierCapsLock);

  uint32_t numMask = 0 | kCGEventFlagMaskNumericPad;
  outMask = keyState.mapModifiersFromOSX(numMask);
  QCOMPARE(outMask, KeyModifierNumLock);
}

void OSXKeyStateTests::rightOptionKeepsItsSide()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  QCOMPARE(state.recorded.size(), size_t(1));
  QCOMPARE(state.recorded[0].id, kKeyAlt_R);
  QCOMPARE(state.recorded[0].button, KeyButton(kVK_RightOption + 1));
  QVERIFY(state.recorded[0].press);
  QVERIFY(!state.isKeyDown(kVK_Option + 1));
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_RightOption);
  QCOMPARE(state.recorded.size(), size_t(2));
  QCOMPARE(state.recorded[1].id, kKeyAlt_R);
  QCOMPARE(state.recorded[1].button, state.recorded[0].button);
  QVERIFY(!state.recorded[1].press);
}

void OSXKeyStateTests::leftOptionKeepsItsSide()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, leftOptionFlags, kVK_Option);
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_Option);
  QCOMPARE(state.recorded.size(), size_t(2));
  QCOMPARE(state.recorded[0].id, kKeyAlt_L);
  QCOMPARE(state.recorded[1].id, kKeyAlt_L);
  QVERIFY(state.recorded[0].press);
  QVERIFY(!state.recorded[1].press);
}

void OSXKeyStateTests::bothOptionsReleaseIndependently()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, leftOptionFlags, kVK_Option);
  state.handleModifierKeys(&state, KeyModifierAlt, KeyModifierAlt, leftOptionFlags | rightOptionFlags, kVK_RightOption);
  state.handleModifierKeys(&state, KeyModifierAlt, KeyModifierAlt, rightOptionFlags, kVK_Option);
  QVERIFY(!state.isKeyDown(kVK_Option + 1));
  QVERIFY(state.isKeyDown(kVK_RightOption + 1));
  QCOMPARE(state.getActiveModifiers(), KeyModifierAlt);
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_RightOption);
  QCOMPARE(state.recorded.size(), size_t(4));
  QCOMPARE(state.recorded[0].id, kKeyAlt_L);
  QCOMPARE(state.recorded[1].id, kKeyAlt_R);
  QCOMPARE(state.recorded[2].id, kKeyAlt_L);
  QCOMPARE(state.recorded[3].id, kKeyAlt_R);
  QVERIFY(state.recorded[0].press && state.recorded[1].press);
  QVERIFY(!state.recorded[2].press && !state.recorded[3].press);
}

void OSXKeyStateTests::repeatedOptionFlagsDoNotDuplicateEvents()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  state.handleModifierKeys(&state, KeyModifierAlt, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_RightOption);
  state.handleModifierKeys(&state, 0, 0, 0, kVK_RightOption);
  QCOMPARE(state.recorded.size(), size_t(2));
}

void OSXKeyStateTests::rightOptionWithoutDeviceFlags()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, kCGEventFlagMaskAlternate, kVK_RightOption);
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_RightOption);
  QCOMPARE(state.recorded.size(), size_t(2));
  QCOMPARE(state.recorded[0].id, kKeyAlt_R);
  QCOMPARE(state.recorded[1].id, kKeyAlt_R);
}

void OSXKeyStateTests::rightOptionSurvivesShiftChanges()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  state.handleModifierKeys(
      &state, KeyModifierAlt, KeyModifierAlt | KeyModifierShift, rightOptionFlags | kCGEventFlagMaskShift, kVK_Shift
  );
  state.handleModifierKeys(&state, KeyModifierAlt | KeyModifierShift, KeyModifierAlt, rightOptionFlags, kVK_Shift);
  QVERIFY(state.isKeyDown(kVK_RightOption + 1));
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_RightOption);
  QCOMPARE(state.recorded.size(), size_t(4));
  QCOMPARE(state.recorded[0].id, kKeyAlt_R);
  QCOMPARE(state.recorded[1].id, kKeyShift_L);
  QCOMPARE(state.recorded[2].id, kKeyShift_L);
  QCOMPARE(state.recorded[3].id, kKeyAlt_R);
  QVERIFY(state.recorded[0].press && state.recorded[1].press);
  QVERIFY(!state.recorded[2].press && !state.recorded[3].press);
}

void OSXKeyStateTests::rightOptionCompatibilityKeepsLeftAndNativeState()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.setRightOptionAsAltGr(true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  QCOMPARE(state.recorded[0].id, kKeyAltGr);
  QCOMPARE(state.recorded[0].mask, KeyModifierAltGr);
  QCOMPARE(state.getActiveModifiers(), KeyModifierAlt);
  state.sendMappedKeyEvent(&state, true, false, 'a', KeyModifierAlt | KeyModifierControl, 1, 1);
  QCOMPARE(state.recorded[1].id, KeyID('a'));
  QCOMPARE(state.recorded[1].mask, KeyModifierAltGr | KeyModifierControl);
  QCOMPARE(state.getActiveModifiers(), KeyModifierAlt);
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_RightOption);
  QCOMPARE(state.recorded[2].id, kKeyAltGr);
  QCOMPARE(state.recorded[2].mask, KeyModifierMask(0));
  state.handleModifierKeys(&state, 0, KeyModifierAlt, leftOptionFlags, kVK_Option);
  QCOMPARE(state.recorded[3].id, kKeyAlt_L);
  QCOMPARE(state.recorded[3].mask, KeyModifierAlt);
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_Option);
  QCOMPARE(state.recorded[4].id, kKeyAlt_L);
  QCOMPARE(state.recorded[4].mask, KeyModifierMask(0));
}

void OSXKeyStateTests::rightOptionCompatibilityReleasesBothSides()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.setRightOptionAsAltGr(true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  state.handleModifierKeys(&state, KeyModifierAlt, KeyModifierAlt, rightOptionFlags | leftOptionFlags, kVK_Option);
  QCOMPARE(state.recorded[1].id, kKeyAlt_L);
  QCOMPARE(state.recorded[1].mask, KeyModifierAlt | KeyModifierAltGr);
  state.handleModifierKeys(&state, KeyModifierAlt, KeyModifierAlt, leftOptionFlags, kVK_RightOption);
  QCOMPARE(state.recorded[2].id, kKeyAltGr);
  QCOMPARE(state.recorded[2].mask, KeyModifierAlt);
  QVERIFY(state.isKeyDown(kVK_Option + 1));
  QVERIFY(!state.isKeyDown(kVK_RightOption + 1));
  state.handleModifierKeys(&state, KeyModifierAlt, 0, 0, kVK_Option);
  QCOMPARE(state.recorded[3].id, kKeyAlt_L);
  QCOMPARE(state.recorded[3].mask, KeyModifierMask(0));
}

void OSXKeyStateTests::lostModifierReleasesRecoverBeforeNextInput()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  const KeyModifierMask modifiers = KeyModifierShift | KeyModifierControl | KeyModifierSuper;
  const CGEventFlags flags = kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskCommand;
  state.handleModifierKeys(&state, 0, modifiers, flags, kVK_Command);
  QCOMPARE(state.recorded.size(), size_t(3));

  // Lose all three flags-changed releases, as in the reported Windows
  // state. The next unmodified input must queue releases before the click.
  state.releaseStaleModifiers(&state, 0);
  QCOMPARE(state.recorded.size(), size_t(6));
  for (size_t i = 0; i < 3; ++i) {
    QCOMPARE(state.recorded[i + 3].id, state.recorded[i].id);
    QCOMPARE(state.recorded[i + 3].button, state.recorded[i].button);
    QVERIFY(!state.recorded[i + 3].press);
    QVERIFY(!state.isKeyDown(state.recorded[i].button));
  }
  QCOMPARE(state.getActiveModifiers(), KeyModifierMask(0));
  state.sendMappedKeyEvent(&state, true, false, 'a', state.getActiveModifiers(), 1, kVK_ANSI_A + 1);
  QCOMPARE(state.recorded.back().mask, KeyModifierMask(0));

  const size_t count = state.recorded.size();
  state.releaseStaleModifiers(&state, 0);
  QCOMPARE(state.recorded.size(), count);
}

void OSXKeyStateTests::heldModifiersSurviveInputSnapshots()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  const KeyModifierMask modifiers = KeyModifierShift | KeyModifierControl | KeyModifierSuper | KeyModifierAlt;
  const CGEventFlags flags =
      kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskCommand | kCGEventFlagMaskAlternate;
  state.handleModifierKeys(&state, 0, modifiers, flags, kVK_Option);
  const size_t count = state.recorded.size();
  // A long drag/shortcut must remain held. Device-specific flags are optional.
  for (int i = 0; i < 1000; ++i)
    state.releaseStaleModifiers(&state, flags);
  QCOMPARE(state.recorded.size(), count);
  QCOMPARE(state.getActiveModifiers(), modifiers);

  // Recover Ctrl/Command/Option while Shift is legitimately still down.
  state.releaseStaleModifiers(&state, kCGEventFlagMaskShift);
  QCOMPARE(state.getActiveModifiers(), KeyModifierShift);
  QVERIFY(state.isKeyDown(kVK_Shift + 1));
  QCOMPARE(state.recorded.size(), count + 3);
}

void OSXKeyStateTests::staleButtonsRecoverAfterMaskRefresh()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  // A refreshed aggregate mask can be clear while side-specific buttons are
  // still down. Recover every side, but preserve Caps Lock and ordinary keys.
  const KeyButton buttons[] = {kVK_Shift + 1,  kVK_RightShift + 1,  kVK_Control + 1, kVK_RightControl + 1,
                               kVK_Option + 1, kVK_RightOption + 1, kVK_Command + 1, kVK_RightCommand + 1};
  for (KeyButton button : buttons)
    state.onKey(button, true, KeyModifierCapsLock);
  state.onKey(kVK_ANSI_A + 1, true, KeyModifierCapsLock);
  state.releaseStaleModifiers(&state, kCGEventFlagMaskAlphaShift);
  QCOMPARE(state.recorded.size(), size_t(8));
  for (KeyButton button : buttons)
    QVERIFY(!state.isKeyDown(button));
  for (const auto &key : state.recorded)
    QVERIFY(!key.press);
  QVERIFY(state.isKeyDown(kVK_ANSI_A + 1));
  QCOMPARE(state.getActiveModifiers(), KeyModifierCapsLock);
}

void OSXKeyStateTests::staleRightOptionUsesCompatibilityMapping()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.setRightOptionAsAltGr(true);
  state.handleModifierKeys(&state, 0, KeyModifierAlt, rightOptionFlags, kVK_RightOption);
  state.releaseStaleModifiers(&state, 0);
  QCOMPARE(state.recorded.size(), size_t(2));
  QCOMPARE(state.recorded.back().id, kKeyAltGr);
  QCOMPARE(state.recorded.back().button, state.recorded.front().button);
  QVERIFY(!state.recorded.back().press);
  QCOMPARE(state.recorded.back().mask, KeyModifierMask(0));
}

void OSXKeyStateTests::modifierRecoveryDoesNotInventPresses()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  RecordingOSXKeyState state(&events, keyMap, {"en"}, true);
  state.releaseStaleModifiers(&state, kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskCommand);
  QVERIFY(state.recorded.empty());
  QCOMPARE(state.getActiveModifiers(), KeyModifierMask(0));
}

void OSXKeyStateTests::translateOrdinaryKeysOnWorkerThread()
{
  deskflow::KeyMap keyMap;
  EventQueue events;
  OSXKeyState state(&events, keyMap, {"en"}, true);
  // Construct events for translation only; never post them to the system.
  CGEventRef event = CGEventCreateKeyboardEvent(nullptr, kVK_ANSI_A, true);
  QVERIFY(event != nullptr);
  CGEventSetFlags(event, 0);
  OSXKeyState::KeyIDs expected;
  KeyModifierMask mask = 0;
  const KeyButton expectedButton = state.mapKeyFromEvent(expected, &mask, event);
  QVERIFY(expectedButton != 0);
  QVERIFY(!expected.empty());
  bool matches = true;
  m_log.setFilter(LogLevel::Level::Error);
  std::unique_ptr<QThread> worker(QThread::create([&] {
    state.updateKeyMap();
    state.pollActiveGroup();
    for (int i = 0; i < 2000; ++i) {
      OSXKeyState::KeyIDs ids;
      KeyModifierMask workerMask = 0;
      matches = (state.mapKeyFromEvent(ids, &workerMask, event) == expectedButton && ids == expected) && matches;
    }
  }));
  QSignalSpy finished(worker.get(), &QThread::finished);
  worker->start();
  // wait() pumps the main Qt loop, as the real core does. A timeout also
  // catches a main-thread dispatch deadlock instead of hanging the test.
  const bool completed = !finished.empty() || finished.wait(15000);
  if (!completed)
    qFatal("Keyboard translation worker did not finish within 15 seconds");
  worker->wait();
  m_log.setFilter(LogLevel::Level::Verbose);
  CFRelease(event);
  QVERIFY(matches);
}

void OSXKeyStateTests::fakePollShift()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(SHIFT_ID_L, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyDown(SHIFT_ID_R, 0, 2, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(2);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));
}

void OSXKeyStateTests::fakePollChar()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

void OSXKeyStateTests::fakePollCharWithModifier()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, KeyModifierShift, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

bool OSXKeyStateTests::isKeyPressed(const OSXKeyState &keyState, KeyButton button)
{
  // HACK: allow os to realize key state changes.
  Arch::sleep(.2);

  IKeyState::KeyButtonSet pressed;
  keyState.pollPressedKeys(pressed);

  IKeyState::KeyButtonSet::const_iterator it;
  for (it = pressed.begin(); it != pressed.end(); ++it) {
    LOG_DEBUG("checking key %d", *it);
    if (*it == button) {
      return true;
    }
  }
  return false;
}

QTEST_MAIN(OSXKeyStateTests)
