#!/usr/bin/env python3
"""Lab7 Item 4 - automatic lighting.

Reads the light level from /dev/spi_demo, decides DARK / BRIGHT with two
thresholds (hysteresis), and calls the existing ./Lab7 program, which writes
the command into /dev/demo (the LED driver).

    sudo python3 auto_light.py --led LED1 --low 400 --high 600
    sudo python3 auto_light.py --led LED1 --low 400 --high 600 --dark-high

Default wiring (3.3V - LDR - node - 10k - GND): darker => LOWER ADC value.
If your ADC goes UP when you cover the sensor, add --dark-high.
"""
import argparse
import os
import re
import subprocess
import sys
import time

STATUS_RE = re.compile(r"(LED[1-4]) Status: ([01])")


def read_sample(fd):
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


def run_lab7(program, led, action=None):
    """Run ./Lab7 LEDx [on|off] and return the 0/1 status it reports."""
    cmd = [program, led] + ([action] if action else [])
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True)
    if result.returncode != 0:
        raise RuntimeError("Lab7 failed (%d): %s" % (result.returncode,
                                                     result.stderr.strip()))
    m = STATUS_RE.search(result.stdout)
    if not m or m.group(1) != led:
        raise RuntimeError("unexpected Lab7 reply: %r" % result.stdout)
    return int(m.group(2))


def is_dark(value, previous, low, high, dark_high):
    """Two-threshold hysteresis. True = dark (LED on)."""
    if value <= low:
        return not dark_high
    if value >= high:
        return dark_high
    return previous  # between thresholds: keep previous mode


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab7", default=os.path.join(here, "led_driver", "Lab7"),
                    help="path of the Lab7 LED program")
    ap.add_argument("--led", default="LED1", choices=["LED1", "LED2", "LED3", "LED4"])
    ap.add_argument("--low", type=int, default=400)
    ap.add_argument("--high", type=int, default=600)
    ap.add_argument("--dark-high", action="store_true",
                    help="use if covering the sensor makes the ADC value go UP")
    ap.add_argument("--interval", type=float, default=0.3)
    ap.add_argument("--dev", default="/dev/spi_demo")
    args = ap.parse_args()

    if not (0 <= args.low < args.high <= 1023):
        sys.exit("need 0 <= low < high <= 1023")
    if not os.access(args.lab7, os.X_OK):
        sys.exit("cannot run %s (build it with gcc first)" % args.lab7)

    print("AUTO {}: low={} high={} {}; Ctrl+C stops and turns LED off".format(
        args.led, args.low, args.high, "dark-high" if args.dark_high else "dark-low"),
        flush=True)

    fd = os.open(args.dev, os.O_RDONLY)
    mode = None  # None = first sample, False = BRIGHT, True = DARK
    try:
        while True:
            value = read_sample(fd)
            if value <= args.low:
                band = "LOW"
            elif value >= args.high:
                band = "HIGH"
            else:
                band = "HOLD"

            new_dark = is_dark(value, mode if mode is not None else False,
                               args.low, args.high, args.dark_high)
            if mode is None or new_dark != mode:
                readback = run_lab7(args.lab7, args.led, "on" if new_dark else "off")
            else:
                readback = run_lab7(args.lab7, args.led)  # query only

            if readback != int(new_dark):
                raise RuntimeError("LED readback mismatch: wanted %d got %d"
                                   % (int(new_dark), readback))
            mode = new_dark
            print("ADC={:4d} band={:<4} mode={:<6} {}={:<3} readback={}".format(
                value, band, "DARK" if mode else "BRIGHT", args.led,
                "ON" if mode else "OFF", readback), flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopping...")
    finally:
        os.close(fd)
        try:
            run_lab7(args.lab7, args.led, "off")
            print("{} turned OFF".format(args.led))
        except Exception as exc:  # report, never pretend it worked
            print("WARNING: could not turn {} off: {}".format(args.led, exc))


if __name__ == "__main__":
    main()
