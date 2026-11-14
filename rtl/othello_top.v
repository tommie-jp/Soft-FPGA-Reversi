// rtl/othello_top.v
//
// firmware にリンクされる最上位モジュール。
// HW 合成は想定していない: ホスト側で Verilator により C++ 化され、
// firmware にリンクされて Pico 2 上で実行される。
//
// 構成:
//   - proto: UART テキストプロトコル本体。内部に
//            game_state / legal_bb / pick_* / coord を持つ。
//
// firmware からは rx/tx の byte 線と strategy (DIP スイッチ) を公開。

`default_nettype none

module othello_top (
    input  wire        clk,
    input  wire        rst,
    // 着手の方式 (DIP スイッチの値。rst 中に取り込む)
    //   0 = pick_lsb      (行優先最初の合法手)
    //   1 = pick_max_gain (反転駒数最大の合法手)
    //   2 = pick_corner   (角優先、なければ pick_lsb)
    //   それ以外 = pick_lsb
    input  wire [3:0]  strategy,
    // RX byte stream (firmware: stdin → 1-cycle pulse)
    input  wire        rx_valid,
    input  wire [7:0]  rx_byte,
    // TX byte stream (firmware: 1 cycle ごとに valid を見て stdout へ)
    output wire        tx_valid,
    output wire [7:0]  tx_byte,
    // 状態の LED 用 (firmware/src/status_leds.cpp が GP2〜GP5 に出す)
    output wire        in_game,
    output wire        err_pulse,
    output wire        pass_pulse,
    output wire [1:0]  strategy_now
);

    proto u_proto (
        .clk      (clk),
        .rst      (rst),
        .strategy (strategy),
        .rx_valid (rx_valid),
        .rx_byte  (rx_byte),
        .tx_valid (tx_valid),
        .tx_byte  (tx_byte),
        .in_game      (in_game),
        .err_pulse    (err_pulse),
        .pass_pulse   (pass_pulse),
        .strategy_now (strategy_now)
    );

endmodule

`default_nettype wire
