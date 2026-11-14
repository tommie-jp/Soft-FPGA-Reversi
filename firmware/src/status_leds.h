// firmware/src/status_leds.h
//
// 外付けの LED 4 個 (GP2〜GP5) で Pico 2 の状態を見せる。
// 配線は Soft-FPGA-TD4 の OUT_PORT と同じ: GPIO → 300Ω → LED → GND (High で点灯)。
//
// ふだんの表示 (1 つの LED に 1 つの意味):
//   LED1 GP2  対局中は点灯 (SB/SW から EB/EW/ED まで)
//   LED2 GP3  1 行受けるたびに 100 ms 点灯
//   LED3 GP4  1 行返すたびに 100 ms 点灯
//   LED4 GP5  エラー応答を返したら 2 秒点灯。パスしたら 3 回点滅
//
// 値の表示 (ふだんの表示より優先): show_value() の間だけ、4 bit の値を 2 進数で出す。
// GP2 が最上位ビット (TD4 と同じ)。起動時の DIP スイッチ、XS で変えた方式に使う。
//
// 時間はループの回数ではなく time_us_32() で測る (DUT の重さで見え方が変わらない)。
#pragma once

#include <cstdint>

namespace status_leds {

void init();

// 4 bit の値を duration_us の間だけ 2 進数で表示する
void show_value(uint8_t value, uint32_t duration_us);

void on_rx_line();
void on_tx_line();
void on_error();
void on_pass();

// メインループから毎回呼ぶ。LED の点灯を決めて GPIO に出す
void update(bool in_game);

}  // namespace status_leds
