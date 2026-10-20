#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Requirements section 7, steps 1-2, against the live device over evdev.

Finds the input device by name, prints absinfo (fuzz/flat) for every axis, then records for
N seconds (keep the sensor still) and reports rate, sequence gaps, timestamp steps, flags,
gyro bias and accel magnitude. Needs read access to /dev/input/event* (group "input").

    python3 tools/verify_evdev.py [seconds] [LSM6DS3|LSM6DS3TR-C]
"""
import fcntl
import math
import os
import select
import struct
import sys
import time

EV_SYN, EV_ABS = 0, 3
ABS = {"ACCEL_X": 0, "ACCEL_Y": 1, "ACCEL_Z": 2, "GYRO_X": 3, "GYRO_Y": 4, "GYRO_Z": 5,
       "TEMPERATURE": 6, "SEQUENCE": 7, "FLAGS": 8, "TIMESTAMP": 0x28}
AVERAGE_N = 8
GYRO_MDPS_PER_LSB = 17.50  # +/-500 dps
ACCEL_MG_PER_LSB = 0.122   # +/-4 g
MOD_2_31 = 1 << 31
EVENT = struct.Struct("llHHi")


def find_device(name):
    for node in sorted(os.listdir("/sys/class/input")):
        if node.startswith("event"):
            with open(f"/sys/class/input/{node}/device/name") as f:
                if f.read().strip() == name:
                    return "/dev/input/" + node
    sys.exit(f"no input device named {name!r}")


def absinfo(fd, code):
    buf = bytearray(24)
    fcntl.ioctl(fd, (2 << 30) | (24 << 16) | (ord("E") << 8) | (0x40 + code), buf)
    return struct.unpack("<6i", buf)


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60
    name = sys.argv[2] if len(sys.argv) > 2 else "LSM6DS3"
    dev = find_device(name)
    print(f"device: {dev} ({name})")
    by_id = [l for l in os.listdir("/dev/input/by-id")
             if os.path.realpath("/dev/input/by-id/" + l) == dev] if os.path.isdir("/dev/input/by-id") else []
    print("by-id:", by_id or "none")

    fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
    state = {}
    print("axes:")
    for field, code in ABS.items():
        value, lo, hi, fuzz, flat, _ = absinfo(fd, code)
        state[code] = value
        ok = "ok" if fuzz == 0 and flat == 0 else "FAIL"
        print(f"  {field:11s} code {code:#04x} min={lo} max={hi} fuzz={fuzz} flat={flat} {ok}")

    reports = []
    end = time.time() + seconds
    while time.time() < end:
        if not select.select([fd], [], [], 0.1)[0]:
            continue
        data = os.read(fd, EVENT.size * 256)
        for off in range(0, len(data), EVENT.size):
            _, _, etype, code, value = EVENT.unpack_from(data, off)
            if etype == EV_ABS:
                state[code] = value
            elif etype == EV_SYN and code == 0:
                reports.append(dict(state))
    os.close(fd)

    n = len(reports)
    if n < 2:
        sys.exit("no reports received")
    seq = [r[ABS["SEQUENCE"]] for r in reports]
    ts = [r[ABS["TIMESTAMP"]] for r in reports]
    gaps = sum(1 for a, b in zip(seq, seq[1:]) if (b - a) % MOD_2_31 != 1)
    steps = [(b - a) % MOD_2_31 for a, b in zip(ts, ts[1:])]
    flags = [r[ABS["FLAGS"]] for r in reports]

    print(f"reports: {n} in {seconds:.0f} s = {n / seconds:.1f} Hz")
    print(f"sequence gaps: {gaps}")
    print(f"timestamp step us: min {min(steps)} mean {sum(steps) / len(steps):.1f} max {max(steps)}")
    for bit, label in ((1, "sample missed"), (2, "usb busy"), (4, "error")):
        print(f"flag {label}: {sum(1 for f in flags if f & bit)} reports")
    gyro = [sum(r[ABS[f"GYRO_{a}"]] for r in reports) / n / AVERAGE_N for a in "XYZ"]
    accel = [sum(r[ABS[f"ACCEL_{a}"]] for r in reports) / n / AVERAGE_N for a in "XYZ"]
    print("gyro mean (bias): " + ", ".join(f"{g:.1f} cnt = {g * GYRO_MDPS_PER_LSB / 1000:.3f} dps" for g in gyro))
    mag = math.sqrt(sum(a * a for a in accel))
    print(f"accel mean: {', '.join(f'{a:.0f}' for a in accel)} cnt; |a| = {mag:.0f} cnt = "
          f"{mag * ACCEL_MG_PER_LSB / 1000:.3f} g (sum ~{mag * AVERAGE_N:.0f})")
    print(f"temperature raw mean: {sum(r[ABS['TEMPERATURE']] for r in reports) / n:.1f}")


if __name__ == "__main__":
    main()
