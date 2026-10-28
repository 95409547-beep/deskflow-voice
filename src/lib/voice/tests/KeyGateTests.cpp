/*
 * SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "voice/KeyGate.h"
#include <iostream>
#include <stdexcept>

using namespace deskflow::voice;
void require(bool value)
{
  if (!value)
    throw std::runtime_error("key gate assertion failed");
}
int main()
{
  KeyGate gate;
  gate.context({"Mac", false, true});
  gate.configure("owner", "Windows", HoldKey::F8, true, 0);
  require(!gate.down(HoldKey::F8, 8, true, 1).consume);
  gate.up(8, 2);
  gate.context({"Windows", true, true});
  for (int i = 0; i < 20; ++i) {
    auto down = gate.down(HoldKey::F8, 8, true, 10 + i);
    require(down.consume && down.events.size() == 1 && down.events.front().kind == KeyEventKind::Down);
    require(gate.down(HoldKey::F8, 8, true, 11 + i).events.empty());
    require(gate.repeat(8));
    require(gate.up(8, 12 + i).consume);
    require(!gate.up(8, 13 + i).consume);
  }
  gate.down(HoldKey::F8, 8, true, 100);
  auto changed = gate.context({"Mac", false, true});
  require(changed.size() == 2 && changed.front().kind == KeyEventKind::Cancel);
  require(gate.up(8, 101).consume); // owned UP remains consumed after switching
  gate.context({"Windows", true, true});
  require(!gate.down(HoldKey::F8, 8, false, 110).consume); // modified shortcut passes through
  gate.configure("owner", "Windows", HoldKey::F8, true, 112);
  require(!gate.down(HoldKey::F8, 8, true, 113).consume); // no mid-hold adoption
  gate.up(8, 114);
  require(gate.down(HoldKey::F8, 8, true, 115).consume);
  auto expired = gate.tick(5112);
  require(expired.size() == 2 && expired.front().kind == KeyEventKind::Cancel);
  require(gate.up(8, 5113).consume);
  require(!gate.down(HoldKey::F8, 8, true, 5114).consume);
  gate.up(8, 5115);
  require(!gate.renew("other", 5116));
  gate.configure("new", "Windows", HoldKey::RightOption, true, 5200);
  require(gate.down(HoldKey::RightOption, 61, true, 5201).consume);
  require(gate.suppressedModifier() == HoldKey::RightOption);
  gate.disarm();
  gate.configure("replacement", "Windows", HoldKey::RightOption, true, 5202);
  require(gate.down(HoldKey::RightOption, 61, true, 5203).events.empty());
  require(gate.up(61, 5204).consume);
  gate.context({"Unpaired", true, true});
  require(!gate.down(HoldKey::RightOption, 61, true, 5205).consume);
  gate.up(61, 5206);
  gate.context({"Windows", true, false});
  require(!gate.down(HoldKey::RightOption, 61, true, 5207).consume);
  gate.up(61, 5208);
  gate.context({"Windows", true, true});
  gate.down(HoldKey::Other, 7, true, 5210);
  require(!gate.down(HoldKey::RightOption, 61, true, 5211).consume); // preserve existing chords
  gate.up(7, 5212);
  gate.up(61, 5213);
  require(gate.down(HoldKey::RightOption, 61, true, 5214).consume);
  gate.up(61, 5215);
  std::cout << "Key routing, repeated hold, lease, ownership and cancellation checks passed; no native input\n";
}
