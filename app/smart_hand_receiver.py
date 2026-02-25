#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import pathlib
import sys
import time
from dataclasses import dataclass
from typing import Any

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

try:
    import pyttsx3
except ImportError:
    pyttsx3 = None


@dataclass
class GesturePacket:
    timestamp: dt.datetime
    gesture: str
    text: str
    confidence: float
    pitch: float
    roll: float
    yaw: float
    flex: list[int]
    raw: str


class TTSEngine:
    def __init__(self, enabled: bool, min_repeat_gap_s: float = 0.8) -> None:
        self.enabled = enabled and pyttsx3 is not None
        self.min_repeat_gap_s = min_repeat_gap_s
        self._last_phrase = ""
        self._last_at = 0.0
        self._engine = pyttsx3.init() if self.enabled else None
        if self._engine:
            self._engine.setProperty("rate", 165)

    def speak(self, phrase: str) -> None:
        if not self.enabled:
            return
        now = time.time()
        if phrase == self._last_phrase and (now - self._last_at) < self.min_repeat_gap_s:
            return
        self._last_phrase = phrase
        self._last_at = now
        self._engine.say(phrase)
        self._engine.runAndWait()


def parse_packet(line: str) -> GesturePacket | None:
    line = line.strip()
    if not line:
        return None
    if not line.startswith("{"):
        return None

    try:
        payload = json.loads(line)
    except json.JSONDecodeError:
        return None

    gesture = str(payload.get("gesture", "NONE"))
    text = str(payload.get("text", ""))
    confidence = float(payload.get("confidence", 0.0))
    pitch = float(payload.get("pitch", 0.0))
    roll = float(payload.get("roll", 0.0))
    yaw = float(payload.get("yaw", 0.0))

    flex_raw: Any = payload.get("flex", [])
    flex: list[int] = []
    if isinstance(flex_raw, list):
        for item in flex_raw[:5]:
            try:
                flex.append(int(item))
            except (TypeError, ValueError):
                flex.append(0)
    while len(flex) < 5:
        flex.append(0)

    return GesturePacket(
        timestamp=dt.datetime.now(),
        gesture=gesture,
        text=text,
        confidence=confidence,
        pitch=pitch,
        roll=roll,
        yaw=yaw,
        flex=flex,
        raw=line,
    )


def choose_port(explicit_port: str | None) -> str:
    if list_ports is None:
        raise SystemExit(
            "pyserial is required. Install with: python3 -m pip install -r requirements.txt"
        )
    if explicit_port:
        return explicit_port

    ports = list(list_ports.comports())
    if not ports:
        raise SystemExit("No serial ports found. Connect Arduino/HC-05 and retry with --port.")

    preferred = []
    for p in ports:
        signature = f"{p.device} {p.description} {p.manufacturer}".lower()
        if any(tok in signature for tok in ("usb", "arduino", "wch", "cp210", "hc-05", "tty")):
            preferred.append(p.device)

    return preferred[0] if preferred else ports[0].device


def ensure_log_file(log_file: str | None) -> pathlib.Path:
    if log_file:
        path = pathlib.Path(log_file).expanduser().resolve()
    else:
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        path = pathlib.Path.cwd() / "logs" / f"session_{stamp}.csv"

    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def open_csv_writer(path: pathlib.Path) -> tuple[Any, csv.writer]:
    handle = path.open("w", newline="", encoding="utf-8")
    writer = csv.writer(handle)
    writer.writerow(
        [
            "timestamp",
            "gesture",
            "text",
            "confidence",
            "pitch",
            "roll",
            "yaw",
            "flex_thumb",
            "flex_index",
            "flex_middle",
            "flex_ring",
            "flex_pinky",
            "raw",
        ]
    )
    return handle, writer


def print_packet(packet: GesturePacket) -> None:
    ts = packet.timestamp.strftime("%H:%M:%S")
    flex_fmt = ",".join(f"{v:03d}" for v in packet.flex)
    print(
        f"[{ts}] gesture={packet.gesture:<12} text={packet.text!r:<14} "
        f"conf={packet.confidence:.2f} pitch={packet.pitch:>6.2f} "
        f"roll={packet.roll:>6.2f} flex=[{flex_fmt}]"
    )


def maybe_build_phrase(packet: GesturePacket, state: dict[str, Any]) -> str | None:
    if packet.gesture == "NONE" or not packet.text:
        return None

    now = time.time()
    if now - state["last_append_at"] < state["append_gap_s"]:
        return None
    state["last_append_at"] = now

    # For single letters append directly, for words append with a leading space.
    if len(packet.text) == 1 and packet.text.isalpha():
        state["phrase"] += packet.text
    else:
        if state["phrase"] and not state["phrase"].endswith(" "):
            state["phrase"] += " "
        state["phrase"] += packet.text
        if not state["phrase"].endswith(" "):
            state["phrase"] += " "

    return state["phrase"].strip()


def run(args: argparse.Namespace) -> int:
    if serial is None:
        raise SystemExit(
            "pyserial is required. Install with: python3 -m pip install -r requirements.txt"
        )
    port = choose_port(args.port)
    log_path = ensure_log_file(args.log_file)
    tts = TTSEngine(enabled=args.speak)

    print(f"Using serial port: {port}")
    print(f"Baud rate: {args.baud}")
    print(f"Logging to: {log_path}")
    if args.speak and pyttsx3 is None:
        print( "TTS disabled: pyttsx3 is not installed.", file=sys.stderr )

    csv_handle, csv_writer = open_csv_writer(log_path)
    phrase_state = {"phrase": "", "last_append_at": 0.0, "append_gap_s": args.append_gap}

    start = time.time()
    packet_count = 0

    try:
        with serial.Serial(port, args.baud, timeout=1.0) as ser:
            print("Listening for gesture packets. Press Ctrl+C to stop.")
            while True:
                if args.duration_s > 0 and (time.time() - start) >= args.duration_s:
                    break

                raw = ser.readline().decode("utf-8", errors="replace").strip()
                if not raw:
                    continue
                if args.print_raw:
                    print(f"RAW: {raw}")

                packet = parse_packet(raw)
                if packet is None:
                    continue
                if packet.gesture == "NONE":
                    continue

                packet_count += 1
                print_packet(packet)

                csv_writer.writerow(
                    [
                        packet.timestamp.isoformat(),
                        packet.gesture,
                        packet.text,
                        f"{packet.confidence:.2f}",
                        f"{packet.pitch:.2f}",
                        f"{packet.roll:.2f}",
                        f"{packet.yaw:.2f}",
                        packet.flex[0],
                        packet.flex[1],
                        packet.flex[2],
                        packet.flex[3],
                        packet.flex[4],
                        packet.raw,
                    ]
                )
                csv_handle.flush()

                phrase = maybe_build_phrase(packet, phrase_state)
                if phrase:
                    print(f"Phrase: {phrase}")

                if packet.text and args.speak:
                    tts.speak(packet.text)

    except KeyboardInterrupt:
        print("\nStopped by user.")
    finally:
        csv_handle.close()

    print(f"Packets processed: {packet_count}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Smart Hand gesture receiver and speech output.")
    parser.add_argument("--port", default=None, help="Serial port (example: /dev/tty.usbmodem1101)")
    parser.add_argument("--baud", default=115200, type=int, help="Serial baud rate")
    parser.add_argument("--speak", action="store_true", help="Enable text-to-speech output")
    parser.add_argument("--log-file", default=None, help="CSV path for gesture logs")
    parser.add_argument(
        "--duration-s",
        default=0,
        type=int,
        help="Stop after N seconds (0 means run until Ctrl+C)",
    )
    parser.add_argument("--append-gap", default=0.9, type=float, help="Min gap between phrase appends")
    parser.add_argument("--print-raw", action="store_true", help="Print raw serial lines")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
