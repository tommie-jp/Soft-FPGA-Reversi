// firmware/src/main.cpp
//
// Pico 2 上の最薄ホスト。プロトコル FSM は rtl/proto.v 側にあるので、
// このファイルは「USB-CDC ↔ DUT バイト線」の中継しかしない。
//
// ループ単位の処理:
//   1. stdin にバイトがあれば取って rx_byte / rx_valid に乗せる
//   2. clk を 1 cycle 進める
//   3. tx_valid が立っていれば tx_byte を stdout に出す
//
// ループは ~1 µs 周期で回るので 115200 baud (= 87 µs/byte) に十分間に合う。
//
// 着手の方式は起動時に DIP スイッチ (GP10〜GP13) を読んで DUT の strategy に渡す。
// 配線は Soft-FPGA-TD4 と同じ: スイッチの片側が GPIO、反対側が GND。内蔵プルアップを使い、
// ON (Low) を 1 と読む。GP10 (スイッチ 1 番) が最上位ビット。

#include <cstdio>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/sysinfo.h"

// リンカスクリプトが定義するシンボル（namespace の外に置かないとリンクできない）
extern "C" {
extern char __flash_binary_start;
extern char __flash_binary_end;
extern char __bss_end__;
}

#include "status_leds.h"
#include "stub_mutex.h"
#include "Vothello_top.h"

namespace {

// DIP スイッチ: GP10 (1 番, MSB) 〜 GP13 (4 番, LSB)
constexpr uint kDipPins[4] = {10, 11, 12, 13};
constexpr uint32_t kDipSettleUs = 100;  // プルアップが効くまで待つ

// 内蔵 LED の点滅 (生きている印)。ループの回数ではなく時間で切り替えるので、
// DUT の重さ (1 tick の長さ) が変わっても 2 Hz のまま
constexpr uint32_t kLedHalfPeriodUs = 250000;

// 外付けの LED 4 個に値を 2 進数で出す時間
constexpr uint32_t kShowDipUs      = 2000000;  // 起動時の DIP スイッチ
constexpr uint32_t kShowStrategyUs = 1000000;  // XS で変えた方式

// 起動時に読んだ DIP スイッチの値 (XI で表示する)
static uint8_t s_dip_value = 0;

uint8_t read_dip_switch() {
    for (const uint pin : kDipPins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }
    sleep_us(kDipSettleUs);
    uint8_t value = 0;
    for (const uint pin : kDipPins) {
        value = static_cast<uint8_t>((value << 1) | (gpio_get(pin) ? 0u : 1u));
    }
    return value;
}

// eval() 実測用アキュムレータ（time_us_32 はマイクロ秒単位）
static uint32_t s_eval_us    = 0;
static uint32_t s_tick_count = 0;

void tick(Vothello_top* dut) {
    const uint32_t t0 = time_us_32();
    dut->clk = 0;
    dut->eval();
    dut->clk = 1;
    dut->eval();
    s_eval_us += time_us_32() - t0;
    ++s_tick_count;
}

// __DATE__ ("Mmm DD YYYY") → "YYYY-MM-DD" に変換する
static void date_iso(char out[11]) {
    static const char kMon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* d = __DATE__;
    int mi = 0;
    for (; mi < 12; ++mi)
        if (d[0]==kMon[mi*3] && d[1]==kMon[mi*3+1] && d[2]==kMon[mi*3+2]) break;
    const int dd = (d[4]==' ') ? (d[5]-'0') : ((d[4]-'0')*10+(d[5]-'0'));
    out[0]=d[7]; out[1]=d[8]; out[2]=d[9]; out[3]=d[10];
    out[4]='-';
    out[5]='0'+(mi+1)/10; out[6]='0'+(mi+1)%10;
    out[7]='-';
    out[8]='0'+dd/10;     out[9]='0'+dd%10;
    out[10]='\0';
}

// XI コマンド応答: デバイス情報 (RUP v0.2 拡張 X* ネームスペース)

void id_report() {
    constexpr uint32_t kFlashKb = PICO_FLASH_SIZE_BYTES / 1024u;
    constexpr uint32_t kRamKb   = 520u;
    const uint32_t prog_kb =
        static_cast<uint32_t>(&__flash_binary_end - &__flash_binary_start) / 1024u;
    // __bss_end__ は SRAM_BASE(0x20000000) からのオフセットで静的使用量を示す
    const uint32_t bss_kb =
        static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&__bss_end__) - 0x20000000u) / 1024u;
    const uint32_t prog_pct = prog_kb * 100u / kFlashKb;
    const uint32_t bss_pct  = bss_kb  * 100u / kRamKb;
    const uint32_t clk_mhz  = clock_get_hz(clk_sys) / 1000000u;
    // CHIP_ID[31:28] = revision (0=B0, 1=B1, 2=B2)
    const uint32_t chip_rev = (sysinfo_hw->chip_id >> 28u) & 0xFu;
    char bld[11];
    date_iso(bld);
    printf("+XI pf=" PICO_PLATFORM_STR
           " chip=B%u flash=%uKB prog=%uKB(%u%%) ram=%uKB bss=%uKB(%u%%)"
           " clk=%uMHz dip=%u git=%s bld=%s\r\n",
           static_cast<unsigned>(chip_rev),
           static_cast<unsigned>(kFlashKb),
           static_cast<unsigned>(prog_kb),  static_cast<unsigned>(prog_pct),
           static_cast<unsigned>(kRamKb),
           static_cast<unsigned>(bss_kb),   static_cast<unsigned>(bss_pct),
           static_cast<unsigned>(clk_mhz),
           static_cast<unsigned>(s_dip_value),
           GIT_HASH,
           bld);
}

// XB コマンド応答: "+XB clk=X.XXXMHz tick=XXXns\r\n" (RUP v0.2 拡張)
// 浮動小数を避け整数演算のみで出力する
void bench_report_protocol() {
    if (s_tick_count == 0) {
        printf("+XB no-data\r\n");
        return;
    }
    // ns_per_tick = us_total * 1000 / ticks
    const uint32_t ns_per_tick = (s_eval_us * 1000u) / s_tick_count;
    // clk_khz = 1_000_000 / ns_per_tick
    const uint32_t clk_khz = (ns_per_tick > 0u) ? (1000000u / ns_per_tick) : 0u;
    printf("+XB clk=%lu.%03luMHz tick=%luns\r\n",
           (unsigned long)(clk_khz / 1000u),
           (unsigned long)(clk_khz % 1000u),
           (unsigned long)ns_per_tick);
}

void apply_reset(Vothello_top* dut, uint8_t strategy) {
    dut->strategy = strategy;  // proto がリセット中に取り込む
    dut->rst = 1;
    dut->rx_valid = 0;
    for (int i = 0; i < 4; ++i) tick(dut);
    dut->rst = 0;
}

}  // namespace

int main() {
    stdio_init_all();
    status_leds::init();

    const uint led_pin = PICO_DEFAULT_LED_PIN;
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);

    sleep_ms(2000);  // USB-CDC ホスト接続待ち
    while (getchar_timeout_us(0) >= 0) {}  // 接続時の余分バイトを flush

    auto* const ctx = new VerilatedContext;
    auto* const dut = new Vothello_top{ctx, "othello_top"};
    s_dip_value = read_dip_switch();
    apply_reset(dut, s_dip_value);
    status_leds::show_value(s_dip_value, kShowDipUs);
    uint8_t last_strategy = dut->strategy_now;

    // XB/XI コマンドインターセプト用ラインバッファ (RUP v0.2 X* 拡張)。
    // 行終端は LF (CR+LF / LF 単独どちらも受け付ける)。
    // "XB" / "XI" は DUT に渡さず C++ 側で応答する。
    char rx_linebuf[36] = {};
    int  rx_linebuf_len = 0;
    bool rx_line_ready  = false;
    int  rx_replay_pos  = 0;

    // RX バックプレッシャ:
    //   USB-CDC は 115200 baud より速くバイトを届ける可能性があるため、DUT が
    //   応答送信中 (S_TX) に次の行を流し込むと proto.v は rx_valid を取りこぼす。
    //   LF を流したら awaiting_response=true にし、以下のいずれかで復帰:
    //     - tx_valid が 1 度立ってから kSettleCycles 連続で 0    (通常応答)
    //     - LF 以後 kMaxSilentCycles 経過しても TX が無い        (EB/EW/ED 等)
    //   Phase A (USB-CDC → rx_linebuf) は次の行を先読みできるので継続。
    bool awaiting_response   = false;
    bool seen_tx_in_response = false;
    int  tx_quiet            = 0;
    int  cycles_since_lf     = 0;
    constexpr int kSettleCycles    = 4;
    constexpr int kMaxSilentCycles = 16;

    bool led = false;
    uint32_t led_toggled_us = time_us_32();
    while (true) {
        const bool dut_ready = !awaiting_response;

        // Phase A: UART → rx_linebuf（ライン未完成のときだけ読む。バックプレッシャ
        // 下でも次行先読みは継続して USB-CDC の取りこぼしを防ぐ）
        if (!rx_line_ready) {
            const int c = getchar_timeout_us(0);
            if (c >= 0) {
                if (rx_linebuf_len < 35)
                    rx_linebuf[rx_linebuf_len++] = static_cast<char>(c);
                if (c == '\n') {
                    status_leds::on_rx_line();
                    // LF で行確定。先頭 2 文字でコマンド判定（CR は無視）
                    const char c0 = rx_linebuf[0];
                    const char c1 = (rx_linebuf_len >= 2) ? rx_linebuf[1] : 0;
                    if (c0 == 'X' && c1 == 'B') {
                        bench_report_protocol();
                        status_leds::on_tx_line();
                        rx_linebuf_len = 0;
                    } else if (c0 == 'X' && c1 == 'I') {
                        id_report();
                        status_leds::on_tx_line();
                        rx_linebuf_len = 0;
                    } else {
                        rx_line_ready = true;
                        rx_replay_pos = 0;
                    }
                }
            }
        }

        // Phase B: rx_linebuf → DUT（1 byte/cycle、DUT 準備済みのときのみ）
        if (rx_line_ready && rx_replay_pos < rx_linebuf_len && dut_ready) {
            const uint8_t b = static_cast<uint8_t>(rx_linebuf[rx_replay_pos++]);
            dut->rx_byte  = b;
            dut->rx_valid = 1;
            if (b == '\n') {
                awaiting_response   = true;
                seen_tx_in_response = false;
                tx_quiet            = 0;
                cycles_since_lf     = 0;
            }
            if (rx_replay_pos >= rx_linebuf_len) {
                rx_line_ready  = false;
                rx_linebuf_len = 0;
                rx_replay_pos  = 0;
            }
        } else {
            dut->rx_valid = 0;
        }

        tick(dut);

        // 状態の LED (DUT の出力は tick 後の値。パルスは 1 tick だけ立つ)
        if (dut->err_pulse)  status_leds::on_error();
        if (dut->pass_pulse) status_leds::on_pass();
        if (dut->strategy_now != last_strategy) {
            last_strategy = dut->strategy_now;
            status_leds::show_value(last_strategy, kShowStrategyUs);
        }
        status_leds::update(dut->in_game);

        // TX: DUT が 1 byte 出していれば stdout に流す
        if (dut->tx_valid) {
            putchar_raw(static_cast<int>(dut->tx_byte));
            if (dut->tx_byte == '\n') status_leds::on_tx_line();
            seen_tx_in_response = true;
            tx_quiet            = 0;
        } else {
            ++tx_quiet;
        }
        if (awaiting_response) {
            ++cycles_since_lf;
            if ((seen_tx_in_response && tx_quiet >= kSettleCycles) ||
                (!seen_tx_in_response && cycles_since_lf >= kMaxSilentCycles)) {
                awaiting_response = false;
            }
        }

        const uint32_t now_us = time_us_32();
        if (now_us - led_toggled_us >= kLedHalfPeriodUs) {
            led_toggled_us = now_us;
            led = !led;
            gpio_put(led_pin, led);
        }
    }
}
