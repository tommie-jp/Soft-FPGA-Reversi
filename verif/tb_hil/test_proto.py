"""RUP v0.2 の応答の HIL テスト。

実機 firmware (rtl/proto.v + firmware/src/main.cpp) の応答を /dev/ttyACM* 経由で確かめる。
仕様は RUP v0.2 (reversi-uart-protocol)。XI / XB は firmware の C++ が直接返すので、
ここ (実機) でしか確かめられない。

各テストの前に conftest の idle_with_lsb が、待ちの状態 + 方式 lsb (XS0) にそろえる。
"""
from __future__ import annotations

import time

import pytest

LSB_NAME = r"^\+VE02SW-FPGA-pico2-reversi-01-lsb\.\d+$"


def _no_response(dut, wait: float = 0.3) -> bytes:
    time.sleep(wait)
    return dut.read(dut.in_waiting or 0)


# ----- クエリ (応答は + か -) -----


def test_pi_returns_plus_pi(dut, expect) -> None:
    dut.write(b"PI\r\n")
    assert expect(dut, r"^\+PI$", timeout=2.0) == "+PI"


def test_pi_can_be_sent_repeatedly(dut, expect) -> None:
    """PI を 5 個まとめて送り、+PI が 5 個返る (行を取りこぼさない)。"""
    dut.write(b"PI\r\n" * 5)
    for _ in range(5):
        expect(dut, r"^\+PI$", timeout=2.0)


def test_ve_returns_name_with_strategy(dut, expect) -> None:
    dut.write(b"VE\r\n")
    expect(dut, LSB_NAME, timeout=2.0)


def test_ca_lists_commands(dut, expect) -> None:
    dut.write(b"CA\r\n")
    line = expect(dut, r"^\+CA ", timeout=2.0)
    tokens = line.split()[1:]
    for cmd in ("PI", "VE", "CA", "SB", "SW", "MO", "PA", "EB", "EW", "ED",
                "XI", "XB", "XS"):
        assert cmd in tokens, f"{cmd} が無い: {line!r}"
    assert "EA" not in tokens, "EA は未対応 (PC は ED を送る前提)"


def test_xi_reports_device(dut, expect) -> None:
    """XI は firmware が直接返す。DIP スイッチの値 (dip=) も入る。"""
    dut.write(b"XI\r\n")
    line = expect(dut, r"^\+XI ", timeout=2.0)
    for key in ("pf=rp2350", "flash=", "clk=", "dip=", "git=", "bld="):
        assert key in line, f"{key} が無い: {line!r}"


def test_xb_reports_tick(dut, expect) -> None:
    dut.write(b"XB\r\n")
    expect(dut, r"^\+XB clk=\d+\.\d{3}MHz tick=\d+ns$", timeout=2.0)


# ----- エラー -----


@pytest.mark.parametrize("cmd", [b"XX\r\n", b"MO\r\n", b"MOz9\r\n", b"BO\r\n", b"EA\r\n"])
def test_unknown_or_bad_returns_minus_01(dut, expect, cmd: bytes) -> None:
    """未知の命令・未対応の命令 (BO、v0.3 案の EA)・形の誤りは -01 unknown。"""
    dut.write(cmd)
    assert expect(dut, r"^-", timeout=2.0) == "-01 unknown"


@pytest.mark.xfail(strict=True, reason="RUP v0.2 は小文字に -02 を求めるが、FPGA は -01 を返す (未対応)")
def test_lowercase_command_returns_minus_02(dut, expect) -> None:
    dut.write(b"pi\r\n")
    assert expect(dut, r"^-", timeout=2.0).startswith("-02")


def test_empty_line_is_ignored(dut, expect) -> None:
    dut.write(b"\r\nPI\r\n")
    assert expect(dut, r".", timeout=2.0) == "+PI"


# ----- 通知 (応答なし) と対局 -----


def test_sw_and_end_have_no_response(dut) -> None:
    for cmd in (b"SW\r\n", b"ED\r\n", b"EB\r\n", b"EW\r\n"):
        dut.write(cmd)
        assert _no_response(dut) == b"", cmd


def test_sb_returns_mo_then_bs(dut, expect) -> None:
    """SB に黒の初手 MOd3 (lsb は決定的) と BS<board> を返す。"""
    dut.write(b"SB\r\n")
    assert expect(dut, r"^MO[a-h][1-8]$", timeout=2.0) == "MOd3"
    bs = expect(dut, r"^BS", timeout=2.0)
    assert len(bs) == 66, f"BS<64char> = 66 char: {bs!r}"


def test_pa_after_sb_returns_my_move(dut, expect) -> None:
    """SB → PA (相手パス) で自分の次の手を MO+BS で返す。"""
    dut.write(b"SB\r\n")
    expect(dut, r"^MO", timeout=2.0)
    expect(dut, r"^BS", timeout=2.0)
    dut.write(b"PA\r\n")
    expect(dut, r"^MO[a-h][1-8]$", timeout=2.0)
    assert len(expect(dut, r"^BS", timeout=2.0)) == 66


def test_pi_during_game_keeps_order(dut, expect) -> None:
    """手の直後の PI には、手 (MO + BS) を返したあとで +PI を返す (PC の区切りの前提)。"""
    dut.write(b"SB\r\nPI\r\n")
    assert expect(dut, r".", timeout=2.0).startswith("MO")
    assert expect(dut, r".", timeout=2.0).startswith("BS")
    assert expect(dut, r".", timeout=2.0) == "+PI"


# ----- XS (着手の方式) -----


@pytest.mark.parametrize("digit,name", [("1", "02-max_gain"), ("2", "03-corner"), ("0", "01-lsb")])
def test_xs_selects_strategy(dut, expect, digit: str, name: str) -> None:
    dut.write(f"XS{digit}\r\n".encode())
    assert expect(dut, r"^\+XS", timeout=2.0) == f"+XS{digit}"
    dut.write(b"VE\r\n")
    assert f"-{name}." in expect(dut, r"^\+VE02", timeout=2.0)


def test_xs_rejected_during_game(dut, expect) -> None:
    dut.write(b"SB\r\n")
    expect(dut, r"^BS", timeout=2.0)
    dut.write(b"XS2\r\n")
    assert expect(dut, r"^-", timeout=2.0) == "-06 in game"
    dut.write(b"XS\r\n")
    assert expect(dut, r"^\+XS", timeout=2.0) == "+XS0"


@pytest.mark.parametrize("cmd", [b"XS3\r\n", b"XS9\r\n", b"XS12\r\n"])
def test_xs_bad_argument(dut, expect, cmd: bytes) -> None:
    dut.write(cmd)
    assert expect(dut, r"^-", timeout=2.0) == "-00 bad arg"
