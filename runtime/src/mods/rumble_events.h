// Gameplay-event controller rumble (roll, swim, crawl, spin attack): one call per Link logic step.
// The pulses go through rumble::event_pulse; the host motor takes the larger of them and the game's.
#pragma once
#include <cstdint>

namespace mods {

// Called once per Link logic step from posMoveFromFootPos (link_move_hooks.cpp, site_023FD39C).
void rumble_step(uint32_t link);

// Per-event toggles (Controls > Rumble): off skips that event's pulses.
bool rumble_roll();
bool rumble_swim();
bool rumble_crawl();
bool rumble_spin();
void set_rumble_roll(bool on);
void set_rumble_swim(bool on);
void set_rumble_crawl(bool on);
void set_rumble_spin(bool on);

}  // namespace mods
