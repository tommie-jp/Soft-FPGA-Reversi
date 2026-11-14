// firmware/src/status_leds.cpp — 外付けの LED 4 個の表示。仕様は status_leds.h
#include "status_leds.h"

#include "pico/stdlib.h"

namespace status_leds {
namespace {

// LED1〜LED4 の GPIO。並びは値の表示の最上位ビットから
constexpr uint kLedPins[4] = {2, 3, 4, 5};

constexpr uint32_t kLineFlashUs  = 100000;   // 受信・送信の点灯
constexpr uint32_t kErrorHoldUs  = 2000000;  // エラーの点灯
constexpr uint32_t kPassBlinkUs  = 150000;   // パスの点滅の半周期
constexpr uint32_t kPassBlinks   = 3;

// 期限つきの点灯。time_us_32() は約 71 分で一周するので、差を符号付きで見る。
// 一度も始めていない期限を「期限内」と取り違えないよう、active で区別する
struct Deadline {
    bool     active = false;
    uint32_t until  = 0;

    void start(uint32_t duration_us) {
        until  = time_us_32() + duration_us;
        active = true;
    }
    bool running(uint32_t now) {
        if (active && static_cast<int32_t>(until - now) <= 0) active = false;
        return active;
    }
};

Deadline s_value_shown;
uint8_t  s_value = 0;
Deadline s_rx;
Deadline s_tx;
Deadline s_err;
uint32_t s_pass_start  = 0;
bool     s_pass_active = false;

void put_all(uint8_t bits) {
    for (int i = 0; i < 4; ++i) {
        gpio_put(kLedPins[i], (bits >> (3 - i)) & 1u);
    }
}

bool pass_led_on(uint32_t now) {
    if (!s_pass_active) return false;
    const uint32_t phase = (now - s_pass_start) / kPassBlinkUs;
    if (phase >= kPassBlinks * 2) {
        s_pass_active = false;
        return false;
    }
    return (phase % 2) == 0;
}

}  // namespace

void init() {
    for (const uint pin : kLedPins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
}

void show_value(uint8_t value, uint32_t duration_us) {
    s_value = value & 0x0Fu;
    s_value_shown.start(duration_us);
}

void on_rx_line() { s_rx.start(kLineFlashUs); }
void on_tx_line() { s_tx.start(kLineFlashUs); }
void on_error()   { s_err.start(kErrorHoldUs); }

void on_pass() {
    s_pass_start  = time_us_32();
    s_pass_active = true;
}

void update(bool in_game) {
    const uint32_t now = time_us_32();
    if (s_value_shown.running(now)) {
        put_all(s_value);
        return;
    }
    const bool led4 = s_err.running(now) || pass_led_on(now);
    const uint8_t bits = static_cast<uint8_t>(
        (in_game ? 8u : 0u) |
        (s_rx.running(now) ? 4u : 0u) |
        (s_tx.running(now) ? 2u : 0u) |
        (led4 ? 1u : 0u));
    put_all(bits);
}

}  // namespace status_leds
