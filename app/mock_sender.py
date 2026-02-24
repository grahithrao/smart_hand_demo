#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import random
import time


GESTURES = [
    {"gesture": "A", "text": "A", "confidence": 0.95, "flex": [22, 82, 79, 80, 77]},
    {"gesture": "B", "text": "B", "confidence": 0.92, "flex": [78, 20, 25, 22, 24]},
    {"gesture": "I_NEED_FOOD", "text": "I need food", "confidence": 0.88, "flex": [73, 86, 82, 28, 31]},
    {"gesture": "HELP", "text": "Help", "confidence": 0.90, "flex": [14, 12, 10, 9, 13]},
]


def jitter(value: float, spread: float) -> float:
    return value + random.uniform(-spread, spread)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Print fake gesture packets to stdout for receiver testing."
    )
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between packets")
    parser.add_argument("--count", type=int, default=30, help="Number of packets to emit")
    args = parser.parse_args()

    for i in range(args.count):
        base = GESTURES[i % len(GESTURES)]
        packet = {
            "gesture": base["gesture"],
            "text": base["text"],
            "confidence": round(jitter(base["confidence"], 0.03), 2),
            "pitch": round(jitter(8.0, 7.0), 2),
            "roll": round(jitter(3.0, 7.0), 2),
            "yaw": round(jitter(30.0, 40.0), 2),
            "flex": [max(0, min(100, int(jitter(v, 4)))) for v in base["flex"]],
        }
        print(json.dumps(packet), flush=True)
        time.sleep(args.interval)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
