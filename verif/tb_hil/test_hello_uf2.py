"""ファームウェア基本疎通の HIL smoke test (RUP v0.2)。

    - PI を投げると +PI が返る
    - VE で +VE02 で始まる名前が返る
    - 何も送らなければ何も出てこない (静かに待機している)

これにより「ファームウェアが起動して、UART を喋れる」基本要件を担保する。
"""
from __future__ import annotations

import time


def test_pi_smoke(dut, expect):
    """ファームが起動していて PI に +PI で応答する。"""
    dut.write(b"PI\r\n")
    assert expect(dut, r"^\+PI$", timeout=5.0) == "+PI"


def test_ve_smoke(dut, expect):
    """VE が +VE02 で始まる名前を返す。"""
    dut.write(b"VE\r\n")
    line = expect(dut, r"^\+VE02", timeout=5.0)
    assert len(line) > len("+VE02"), f"名前が無い: {line!r}"


def test_idle_serial_is_quiet(dut):
    """無入力時に不審な余分バイトが出てこない (0.5s 採取して空)。"""
    time.sleep(0.5)
    leftover = dut.read(dut.in_waiting or 0)
    assert not leftover, f"アイドル時に余分なバイト: {leftover!r}"
