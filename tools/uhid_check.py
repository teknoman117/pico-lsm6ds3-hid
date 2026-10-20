#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check the host-side HID mapping without hardware via /dev/uhid (needs root).

Loads report_desc from the built ELF, creates a virtual USB HID device,
prints the evdev capabilities and absinfo the kernel assigns, then sends reports
and prints the resulting events.

    sudo python3 tools/uhid_check.py [build/pico-lsm6ds3.elf] [LSM6DS3|LSM6DS3TR-C]
"""
import fcntl
import glob
import os
import select
import struct
import sys
import time

UHID_DESTROY, UHID_CREATE2, UHID_INPUT2 = 1, 11, 12
UHID_EVENT_SIZE = 4 + 4372
BUS_USB = 3
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
ABS_NAMES = {0: "ABS_X", 1: "ABS_Y", 2: "ABS_Z", 3: "ABS_RX", 4: "ABS_RY", 5: "ABS_RZ",
             6: "ABS_THROTTLE", 7: "ABS_RUDDER", 8: "ABS_WHEEL", 0x28: "ABS_MISC"}
# gyro_sum[3], accel_sum[3], timestamp, sequence, temperature, flags
REPORT = struct.Struct("<6i2IhB")


def elf_symbol(path, name):
    data = open(path, "rb").read()
    assert data[:4] == b"\x7fELF" and data[4] == 1, "expected ELF32"
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    for sh in sections:
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for off in range(sh[4], sh[4] + sh[5], 16):
            st_name, st_value, st_size, _, _, st_shndx = struct.unpack_from("<IIIBBH", data, off)
            end = data.index(b"\0", strtab[4] + st_name)
            if data[strtab[4] + st_name:end].decode() == name:
                sec = sections[st_shndx]
                start = sec[4] + st_value - sec[3]
                return data[start:start + st_size]
    raise KeyError(name)


def uhid_write(fd, ev_type, payload):
    buf = struct.pack("<I", ev_type) + payload
    os.write(fd, buf.ljust(UHID_EVENT_SIZE, b"\0"))


def find_event_node(name, timeout=3.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for path in sorted(glob.glob("/sys/class/input/event*"), key=os.path.getmtime, reverse=True):
            try:
                with open(f"{path}/device/name") as f:
                    if f.read().strip() == name and "uhid" in os.path.realpath(path):
                        return "/dev/input/" + os.path.basename(path), path
            except OSError:
                pass
        time.sleep(0.05)
    raise RuntimeError("virtual device did not appear")


def bits(sysfs_hex):
    words = sysfs_hex.split()
    value = 0
    for w in words:
        value = (value << 64) | int(w, 16)
    return {i for i in range(value.bit_length()) if value >> i & 1}


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "build/pico-lsm6ds3.elf"
    name = sys.argv[2] if len(sys.argv) > 2 else "LSM6DS3"
    rd = elf_symbol(elf, "report_desc")
    print(f"report descriptor: {len(rd)} bytes")

    fd = os.open("/dev/uhid", os.O_RDWR)
    create = struct.pack("<128s64s64sHHIIII", name.encode(), b"uhid-check", b"", len(rd), BUS_USB,
                         0x2E8A, 0x000A, 0x0100, 0) + rd
    uhid_write(fd, UHID_CREATE2, create)
    try:
        dev, sysfs = find_event_node(name)
        caps = sysfs + "/device/capabilities"
        abs_bits = bits(open(caps + "/abs").read())
        key_bits = bits(open(caps + "/key").read())
        print(f"node: {dev}")
        print("keys:", [hex(k) for k in sorted(key_bits)])
        print("abs: ", [ABS_NAMES.get(a, hex(a)) for a in sorted(abs_bits)])

        evfd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
        for a in sorted(abs_bits):
            info = bytearray(24)
            fcntl.ioctl(evfd, (2 << 30) | (24 << 16) | (ord("E") << 8) | (0x40 + a), info)
            _, lo, hi, fuzz, flat, _ = struct.unpack("<6i", info)
            print(f"  {ABS_NAMES.get(a, hex(a)):9s} min={lo} max={hi} fuzz={fuzz} flat={flat}")

        ts = 0x7FFFF000  # crosses the 2^31 wrap
        for i in range(5):
            ts = (ts + 4800) & 0x7FFFFFFF
            report = REPORT.pack(100 * i, -100 * i, 4 * i, 0, 0, 32790, ts, i, 400 + i, 0)
            uhid_write(fd, UHID_INPUT2, struct.pack("<H", len(report)) + report)
            time.sleep(0.01)

        print("events:")
        ev = struct.Struct("llHHi")
        time.sleep(0.05)
        while select.select([evfd], [], [], 0)[0]:
            data = os.read(evfd, ev.size * 64)
            for off in range(0, len(data), ev.size):
                _, _, t, code, value = ev.unpack_from(data, off)
                if t == EV_ABS:
                    print(f"  {ABS_NAMES.get(code, hex(code)):9s} {value}")
                elif t == EV_SYN:
                    print("  --- SYN_REPORT")
        os.close(evfd)
    finally:
        uhid_write(fd, UHID_DESTROY, b"")
        os.close(fd)


if __name__ == "__main__":
    main()
