// Controller rumble: the game's motor requests and the host motor level (see rumble.h).
#include "rumble.h"
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace rumble {

void Motor::gamepad_pattern(const uint8_t* bits, uint32_t nbits, uint64_t now) {
    nbits_ = bits ? std::min(nbits, kMaxBits) : 0;  // no pattern: still, as an empty one
    memset(pattern_, 0, sizeof pattern_);
    if (nbits_) memcpy(pattern_, bits, (nbits_ + 7) / 8);
    start_ = now;
}

void Motor::gamepad_stop() { nbits_ = 0; }

void Motor::pro_motor(bool on, uint64_t now) {
    pro_on_ = on;
    pro_time_ = now;
}

void Motor::reset() {
    nbits_ = 0;
    pro_on_ = false;
}

float Motor::level(uint64_t now, uint64_t window) const {
    if (pro_on_ && now - pro_time_ < kProStaleUs) return 1.0f;
    if (!nbits_ || now < start_) return 0.0f;
    // the pattern bits under [now, now + window); bits past the end are still
    const uint64_t t = now - start_;
    const uint64_t first = t / kBitUs, last = std::max(first + 1, (t + window + kBitUs - 1) / kBitUs);
    if (first >= nbits_) return 0.0f;
    uint32_t on = 0;
    for (uint64_t i = first; i < last && i < nbits_; i++) on += (pattern_[i >> 3] >> (i & 7)) & 1;
    return float(on) / float(last - first);
}

bool Motor::active(uint64_t now) const {
    return (pro_on_ && now - pro_time_ < kProStaleUs) || (nbits_ && now - start_ < uint64_t(nbits_) * kBitUs);
}

// ---- the shared state
namespace {
// Pulses from gameplay events (roll, swim, sneak), each held for its duration. A new pulse follows the
// ones already queued, so two calls make a double pulse.
class EventSource {
public:
    void pulse(float strength, uint64_t hold_us, uint64_t now) {
        if (!(hold_us > 0)) return;
        compact(now);
        if (count_ >= kMax) return;
        uint64_t start = now;
        if (count_) start = std::max(start, entries_[count_ - 1].start + entries_[count_ - 1].hold_us);
        entries_[count_++] = {std::clamp(strength, 0.f, 1.f), start, hold_us};
    }
    float level(uint64_t now) const {
        float level = 0.f;
        for (int i = 0; i < count_; i++)
            if (now >= entries_[i].start && now - entries_[i].start < entries_[i].hold_us)
                level = std::max(level, entries_[i].strength);
        return level;
    }
    void reset() { count_ = 0; }

private:
    static constexpr int kMax = 2;
    struct Entry { float strength; uint64_t start, hold_us; };
    void compact(uint64_t now) {
        int n = 0;
        for (int i = 0; i < count_; i++)
            if (now < entries_[i].start + entries_[i].hold_us) entries_[n++] = entries_[i];
        count_ = n;
    }
    Entry entries_[kMax] = {};
    int count_ = 0;
};

std::mutex g_mu;
Motor g_motor;
EventSource g_events;

uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// WWHD_RUMBLE=0 starts with the motors off (and the saved option is not read); any other value on
const char* env() { return getenv("WWHD_RUMBLE"); }
std::atomic<bool> g_enabled{!env() || atoi(env()) != 0};
}  // namespace

bool log_enabled() {
    static const bool on = getenv("WWHD_LOG_RUMBLE") || getenv("WWHD_RUMBLE_LOG");  // (the earlier name)
    return on;
}

void gamepad_pattern(uint32_t chan, const uint8_t* bits, uint32_t nbits) {
    if (chan != 0) return;
    if (log_enabled()) {
        char hex[2 * Motor::kMaxBits / 8 + 1] = {};
        for (uint32_t i = 0; bits && i < (std::min(nbits, Motor::kMaxBits) + 7) / 8; i++) snprintf(hex + 2 * i, 3, "%02x", bits[i]);
        LOG("[rumble] %.3f game: GamePad pattern of %u bits (%s), %.0f ms", now_us() / 1e6, nbits, hex, std::min(nbits, Motor::kMaxBits) * Motor::kBitUs / 1000.0);
    }
    std::lock_guard lk(g_mu);
    g_motor.gamepad_pattern(bits, nbits, now_us());
}

void gamepad_stop(uint32_t chan) {
    if (chan != 0) return;
    if (log_enabled()) LOG("[rumble] %.3f game: GamePad motor stop", now_us() / 1e6);
    std::lock_guard lk(g_mu);
    g_motor.gamepad_stop();
}

void pro_motor(uint32_t chan, bool on) {
    if (chan != 0) return;
    const uint64_t now = now_us();
    std::lock_guard lk(g_mu);
    // the game repeats the state every frame while an effect runs: log the changes only
    static bool logged = false;
    if (log_enabled() && on != logged) LOG("[rumble] %.3f game: Pro Controller motor %s", now / 1e6, on ? "on" : "off");
    logged = on;
    g_motor.pro_motor(on, now);
}

void event_pulse(float strength, uint64_t hold_us) {
    std::lock_guard lk(g_mu);
    g_events.pulse(strength, hold_us, now_us());
}

void reset() {
    if (log_enabled()) LOG("[rumble] save state loaded: motors still");
    std::lock_guard lk(g_mu);
    g_motor.reset();
    g_events.reset();
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
void set_enabled(bool on) {
    if (g_enabled.exchange(on) != on) LOG("[rumble] controller rumble %s", on ? "on" : "off");
}
bool env_override() { return env() != nullptr; }

float host_level(uint64_t window_us) {
    if (!enabled()) return 0.0f;
    std::lock_guard lk(g_mu);
    const uint64_t now = now_us();
    return std::max(g_motor.level(now, window_us), g_events.level(now));
}

}  // namespace rumble
