// Gameplay-event controller rumble: roll, swim, crawl and spin-attack pulses (rumble_events.h).
#include "rumble_events.h"

#include <algorithm>
#include <atomic>
#include <string>

#include "guest_addr.h"
#include "move_speed.h"
#include "play_context.h"
#include "runtime.h"
#include "../overlay/hostui.h"
#include "../rumble.h"

namespace mods {
namespace {
constexpr uint32_t kSpeedF = 0x370;  // fopAc_ac_c::speedF
constexpr float kMoveThreshold = 0.5f, kFullSpeed = 12.f;
constexpr uint32_t kRollDelaySteps = 6, kPulseIntervalSteps = 15, kCrawlGapSteps = 3, kSpinPulseSteps = 8;
constexpr uint32_t kSwimIntervalSteps = 30;  // ~1 s between swim pulses at low speed
constexpr uint64_t kRollHoldUs = 180000, kSwimHoldUs = 120000, kCrawlHoldUs = 40000;
constexpr uint64_t kSpinHoldUs = 80000, kSpinReleaseHoldUs = 250000;
constexpr float kRollStrength = 0.07f, kSwimStrength = 0.083f, kCrawlStrength = 0.18f;
constexpr float kSpinStrength = 0.12f, kSpinReleaseStrength = 0.85f;

uint32_t g_prev_proc = ~0u;
uint32_t g_roll_wait = 0;
uint32_t g_swim_wait = 0;
uint32_t g_crawl_wait = 0, g_crawl_gap = 0;
uint32_t g_spin_wait = 0;
float g_crawl_strength = 0.f;
bool g_spin_charged = false;

float speed_scale(float speed) { return std::clamp(speed / kFullSpeed, 0.f, 1.f); }

// Each toggle loads its saved value once; unset means off, a saved "1" means on.
bool load_toggle(const char* key) {
    std::string value;
    return hostui::get(key, value) && value == "1";
}
std::atomic<bool>& roll_toggle() {
    static std::atomic<bool> on{load_toggle("rumble.roll")};
    return on;
}
std::atomic<bool>& swim_toggle() {
    static std::atomic<bool> on{load_toggle("rumble.swim")};
    return on;
}
std::atomic<bool>& crawl_toggle() {
    static std::atomic<bool> on{load_toggle("rumble.crawl")};
    return on;
}
std::atomic<bool>& spin_toggle() {
    static std::atomic<bool> on{load_toggle("rumble.spin")};
    return on;
}
}  // namespace

bool rumble_roll() { return roll_toggle().load(std::memory_order_relaxed); }
bool rumble_swim() { return swim_toggle().load(std::memory_order_relaxed); }
bool rumble_crawl() { return crawl_toggle().load(std::memory_order_relaxed); }
bool rumble_spin() { return spin_toggle().load(std::memory_order_relaxed); }
void set_rumble_roll(bool on) {
    roll_toggle().store(on, std::memory_order_relaxed);
    hostui::set("rumble.roll", on ? "1" : "0");
}
void set_rumble_swim(bool on) {
    swim_toggle().store(on, std::memory_order_relaxed);
    hostui::set("rumble.swim", on ? "1" : "0");
}
void set_rumble_crawl(bool on) {
    crawl_toggle().store(on, std::memory_order_relaxed);
    hostui::set("rumble.crawl", on ? "1" : "0");
}
void set_rumble_spin(bool on) {
    spin_toggle().store(on, std::memory_order_relaxed);
    hostui::set("rumble.spin", on ? "1" : "0");
}

void rumble_step(uint32_t link) {
    const uint32_t proc = ld32(link + playctx::kLinkCurProc);
    const uint32_t mode = ld32(link + playctx::kLinkModeFlg);
    const uint32_t status0 = ld32(GD(0x1046F0B0) + playctx::kPlayerStatus0);
    const float speed = u32_as_f32(ld32(link + kSpeedF));
    const bool moving = speed > kMoveThreshold;

    // roll: one pulse partway through the animation
    if (rumble_roll()) {
        if (g_roll_wait > 0) {
            if (--g_roll_wait == 0) rumble::event_pulse(kRollStrength, kRollHoldUs);
        } else if (proc == kProcFrontRoll && g_prev_proc != kProcFrontRoll) {
            g_roll_wait = kRollDelaySteps;
        }
    }

    // swim: a pulse every ~1 s while moving
    if (rumble_swim()) {
        if (proc == kProcSwimMove && moving) {
            if (g_swim_wait > 0) --g_swim_wait;
            if (g_swim_wait == 0) {
                rumble::event_pulse(kSwimStrength * speed_scale(speed), kSwimHoldUs);
                g_swim_wait = kSwimIntervalSteps;
            }
        } else {
            g_swim_wait = 0;
        }
    }

    // crawl: two short pulses ~90 ms apart every ~0.5 s while moving
    if (rumble_crawl()) {
        if (((mode & playctx::kModeCrawl) || (status0 & playctx::kSt0Crawl)) && moving) {
            if (g_crawl_gap > 0 && --g_crawl_gap == 0)
                rumble::event_pulse(g_crawl_strength, kCrawlHoldUs);
            if (g_crawl_wait > 0) --g_crawl_wait;
            if (g_crawl_wait == 0) {
                g_crawl_strength = kCrawlStrength * speed_scale(speed);
                rumble::event_pulse(g_crawl_strength, kCrawlHoldUs);
                g_crawl_gap = kCrawlGapSteps;
                g_crawl_wait = kPulseIntervalSteps;
            }
        } else {
            g_crawl_wait = 0;
            g_crawl_gap = 0;
        }
    }

    // spin attack: a small pulse while the charge is held, one big pulse on release
    const bool spin_charged = (status0 & playctx::kSt0SpinAttack) != 0;
    if (rumble_spin()) {
        if (spin_charged) {
            if (g_spin_wait > 0) --g_spin_wait;
            if (g_spin_wait == 0) {
                rumble::event_pulse(kSpinStrength, kSpinHoldUs);
                g_spin_wait = kSpinPulseSteps;
            }
        } else {
            if (g_spin_charged) rumble::event_pulse(kSpinReleaseStrength, kSpinReleaseHoldUs);
            g_spin_wait = 0;
        }
    }
    g_spin_charged = spin_charged;
    g_prev_proc = proc;
}

}  // namespace mods
