#!/usr/bin/env python3
"""Lab7 Item 3 - continuously read the light sensor value from /dev/spi_demo.

    sudo python3 adc_watch.py              # default 0.2 s per sample
    sudo python3 adc_watch.py --interval 0.5
"""
import argparse
import os
import time


def read_sample(fd):
    """Read one line like b'512\\n' from the driver and return 512."""
    line = b""
    while not line.endswith(b"\n"):
        chunk = os.read(fd, 16)
        if not chunk:
            raise EOFError("ADC device returned EOF")
        line += chunk
        if len(line) > 8:
            raise ValueError("ADC record too long: %r" % line)
    value = int(line.strip())
    if not 0 <= value <= 1023:
        raise ValueError("ADC value out of range: %d" % value)
    return value


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dev", default="/dev/spi_demo")
    ap.add_argument("--interval", type=float, default=0.2)
    args = ap.parse_args()

    fd = os.open(args.dev, os.O_RDONLY)
    print("MCP3008 ADC stream (0..1023). Ctrl+C to stop.", flush=True)
    try:
        while True:
            value = read_sample(fd)
            bar = "#" * (value * 40 // 1023)
            print("ADC={:4d} |{:<40}|".format(value, bar), flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
