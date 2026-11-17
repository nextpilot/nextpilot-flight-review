#!/usr/bin/env python3
"""生成脚本自动化用的 seed 飞控日志（CSV）。

5 架次，50 Hz，列 t,roll,pitch,yaw,alt,vz；roll 幅值各不同，
便于用 A/√2 校验 RMS、用 0.3 Hz 校验 FFT 峰值。

用法：python3 tools/gen-seed-flights.py /tmp/ps_seed
"""
import csv
import math
import os
import random
import sys

FLIGHTS = [
    ("flight_01", 60.0, 10.0),
    ("flight_02", 75.0, 15.0),
    ("flight_03", 45.0, 20.0),
    ("flight_04", 90.0, 12.0),
    ("flight_05", 50.0, 18.0),
]
FS = 50.0


def main(out_dir: str) -> int:
    os.makedirs(out_dir, exist_ok=True)
    random.seed(20261117)
    for name, dur, amp in FLIGHTS:
        n = int(dur * FS)
        with open(os.path.join(out_dir, f"{name}.csv"), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["t", "roll", "pitch", "yaw", "alt", "vz"])
            for i in range(n):
                t = i / FS
                roll = amp * math.sin(2 * math.pi * 0.3 * t) + random.gauss(0, 0.5)
                pitch = 5.0 * math.sin(2 * math.pi * 0.1 * t) + random.gauss(0, 0.3)
                yaw = 30.0 * t / dur + random.gauss(0, 0.2)
                alt = 100 + 20 * math.sin(2 * math.pi * 0.02 * t)
                vz = 0.4 * math.cos(2 * math.pi * 0.02 * t)
                w.writerow([f"{t:.3f}", f"{roll:.4f}", f"{pitch:.4f}",
                            f"{yaw:.4f}", f"{alt:.3f}", f"{vz:.4f}"])
        print(f"{name}.csv {n} points")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/ps_seed"))
