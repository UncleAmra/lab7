# Lab 7: Step-by-step guide (Jetson TX2 drivers + hardware)

You build 4 things. Each one is graded on its own, so finish and get each one checked before you start the next.

| Item | What you build | Points | What the TA checks |
|---|---|---|---|
| 1 | `demo.ko` + `Lab7` program, 4 LEDs | 20% | `./Lab7 LED1 on/off`, `./Lab7 LED1` → `LED1 Status: 0`, and the 5 "Enter ... function" messages in dmesg |
| 2 | `button_irq.ko`, push-button interrupt | 10% | Pressing the button prints `Button Pressed! Interrupt Triggered.` in dmesg |
| 3 | `spi_demo.ko` + `adc_watch.py`, light sensor through MCP3008 | 20% | Python keeps printing ADC values, and the value changes when you cover the sensor or shine light on it |
| 4 | `auto_light.py` | 20% | Covering the sensor turns the LED on, light turns it off, and the LED goes through `/dev/demo` |

**Deadlines from the PDF:** demo (驗收) by **12/2, 18:00**. Group report by **12/9, 23:59**. Each week late = ×0.8.

---

## 0. Files in this folder

```
Lab7/
  led_driver/     demo.c   Lab7.c   Makefile      ← Item 1 (+ used by Item 4)
  button_driver/  button_irq.c      Makefile      ← Item 2
  adc_driver/     spi_demo.c        Makefile      ← Item 3
  adc_watch.py                                    ← Item 3
  auto_light.py                                   ← Item 4
```

---

## 1. Get ready (do this once)

### 1.1 Copy the folder to the TX2
Use FileZilla or a USB stick. Put the folder at **`/home/nvidia/Lab7`** (`~/Lab7`).
All the commands below assume that location.

### 1.2 Open a terminal on the TX2 and check the kernel
```bash
uname -r                                  # should be 4.9.xxx-tegra
uname -m                                  # should be aarch64
ls -ld /lib/modules/$(uname -r)/build     # must exist
```
If the `build` folder **does not exist**, find the headers with `ls /usr/src`, then build with
`make KDIR=/usr/src/<the-linux-headers-folder>`. If you still can't build, ask the TA.

> ⚠️ Build **on the TX2**, not on your PC or VM. A `.ko` built on x86 will not load on the TX2.

### 1.3 J21 40-pin header map: everything this lab uses
Pin 1 is the pin with the **square pad / triangle mark**. Odd-numbered pins are on one row and even-numbered pins on the other.

| Use | J21 pin | Linux GPIO |
|---|---|---|
| LED1 | 7 | 396 |
| LED2 | 15 | 255 |
| LED3 | 16 | 296 |
| LED4 | 22 | 298 |
| Button signal | 18 | 481 |
| MCP3008 CLK | 23 | 427 |
| MCP3008 DOUT (MISO) | 21 | 428 |
| MCP3008 DIN (MOSI) | 19 | 429 |
| MCP3008 CS | 24 | 430 |
| 3.3 V | 1 or 17 | – |
| GND | 6, 9, 14, 20, 25, 30, 34, 39 | – |

> The GPIO numbers come from your lecture. Step 2.2 checks the physical pin of each LED, so if one doesn't light up there, move that wire before you go on.

---

## 2. Item 1: 4-LED character driver (20%)

### 2.1 Wiring (power OFF while you wire)
For **each** LED:
```
J21 pin (7 / 15 / 16 / 22) ── 220Ω–330Ω resistor ── LED long leg (+)
LED short leg (−) ── GND (e.g. pin 6 / 9 / 14 / 20)
```
All 4 LEDs can share one GND rail on the breadboard.

### 2.2 Test the wiring by hand first (so you know the hardware works)
```bash
sudo su
cd /sys/class/gpio
echo 396 > export
echo out > gpio396/direction
echo 1 > gpio396/value      # LED1 should turn ON
echo 0 > gpio396/value      # LED1 OFF
echo 396 > unexport
exit
```
Repeat with 255, 296 and 298 for LED2–LED4. **Always `unexport` at the end.** The driver will export the pins itself.

### 2.3 Build
```bash
cd ~/Lab7/led_driver
make
gcc -Wall -O2 -o Lab7 Lab7.c
ls -l demo.ko Lab7          # both must exist
```

### 2.4 Load the driver and create `/dev/demo`
```bash
sudo insmod ./demo.ko
dmesg | tail -n 5                                   # expect "demo: started, major=XXX"
ledmajor=$(awk '$2=="demo" {print $1}' /proc/devices)
echo $ledmajor                                      # must print a number
sudo rm -f /dev/demo                                # remove an old node if one exists
sudo mknod /dev/demo c $ledmajor 0
ls -l /dev/demo
```

### 2.5 Test it (this is what you demo)
```bash
sudo ./Lab7 LED1 on       # LED1 lights up → prints "LED1 Status: 1"
sudo ./Lab7 LED3 on       # LED3 lights up, LED1 STAYS on
sudo ./Lab7 LED1          # query → "LED1 Status: 1"
sudo ./Lab7 LED1 off      # LED1 off, LED3 still on
sudo ./Lab7 LED1          # → "LED1 Status: 0"
sudo ./Lab7 LED2 on ; sudo ./Lab7 LED4 on
sudo ./Lab7 LED2 off; sudo ./Lab7 LED3 off; sudo ./Lab7 LED4 off
dmesg | tail -n 30
```
In `dmesg` you must see all 5:
```
Enter Open function
Enter I/O Control function
Enter Write function
Enter Read function
Enter Release function
```
📸 **Screenshot:** the terminal showing the commands and `Status` replies, the `dmesg` output, and photos of the LEDs.
➡️ **Show the TA now.** Keep `demo.ko` loaded because Item 4 uses it.

---

## 3. Item 2: button interrupt (10%)

### 3.1 Wiring
Bare 4-leg button with a **10 kΩ pull-up** (released = 1, pressed = 0):
```
3.3V (pin 1 or 17) ── 10kΩ ──┬── J21 pin 18 (GPIO481)
                             │
                          [button]
                             │
                            GND (pin 20)
```
- A 4-leg button has two pairs of legs that are always connected. Use one leg from **each** pair, diagonal legs are the safe choice.
- If you have a **button module** with pins `VCC / GND / OUT`, connect VCC→3.3V (pin 1), GND→pin 20, OUT→pin 18. The module already has the pull-up.
- ❌ Never connect anything to 5 V here.

### 3.2 Check the button by hand first
```bash
echo 481 | sudo tee /sys/class/gpio/export
echo in  | sudo tee /sys/class/gpio/gpio481/direction
watch -n 0.2 cat /sys/class/gpio/gpio481/value
```
You should see **1 when released and 0 when pressed**. Press Ctrl+C to stop, then **give the pin back**. This step is required, or the driver fails with `-16`:
```bash
echo 481 | sudo tee /sys/class/gpio/unexport
```
If the value never changes, recheck the wires, the pull-up resistor and GND before you continue.

### 3.3 Build and load
```bash
cd ~/Lab7/button_driver
make
sudo insmod ./button_irq.ko gpio=481 active_low=1 debounce_ms=30
dmesg | tail -n 5          # "button_irq: loaded: gpio=481 irq=XXX edge=falling ..."
grep lab_button_irq /proc/interrupts
```

### 3.4 Test it (this is what you demo)
Terminal A:
```bash
sudo dmesg -w | grep --line-buffered -E 'button_irq|Button Pressed'
```
Press the button a few times, about one second apart. Each press shows:
```
button_irq: Button Pressed! Interrupt Triggered. count=1
button_irq: Button Pressed! Interrupt Triggered. count=2
```
Terminal B: `grep lab_button_irq /proc/interrupts`. The counts go up after each press.

📸 **Screenshot** both terminals. ➡️ **Show the TA.** Then unload it:
```bash
sudo rmmod button_irq
```

---

## 4. Item 3: light sensor + MCP3008 through software SPI (20%)

### 4.1 Wiring
**MCP3008** (notch at the top; pin 1 is top-left, pins 1–8 run down the left side and 9–16 run up the right side):

| MCP3008 pin | Connect to |
|---|---|
| 16 VDD | 3.3 V (J21 pin 1 / 17) |
| 15 VREF | 3.3 V |
| 14 AGND | GND |
| 13 CLK | J21 **pin 23** |
| 12 DOUT | J21 **pin 21** |
| 11 DIN | J21 **pin 19** |
| 10 CS | J21 **pin 24** |
| 9 DGND | GND |
| 1 CH0 | light-sensor divider middle point |

**Light sensor (LDR) voltage divider:**
```
3.3V ── LDR ──┬── 10kΩ ── GND
              │
          MCP3008 CH0 (pin 1)
```
With this wiring, **darker = lower ADC value**.
If you have an LDR module with an `AO` pin, connect AO directly to CH0 and power the module from 3.3 V.

### 4.2 Make sure the 4 SPI pins are free
```bash
sudo cat /sys/kernel/debug/gpio | grep -E 'gpio-(427|428|429|430)'
ls -d /sys/class/gpio/gpio42* /sys/class/gpio/gpio430 2>/dev/null
```
If any of them shows as exported, unexport it (`echo 427 | sudo tee /sys/class/gpio/unexport`).
**Do not** export these pins by hand. The driver requests them itself.

### 4.3 Build, load and create `/dev/spi_demo`
```bash
cd ~/Lab7/adc_driver
make clean && make
sudo insmod ./spi_demo.ko channel=0
dmesg | tail -n 5                               # "spi_demo: loaded, major=XXX channel=0"
adcmajor=$(awk '$2=="spi_demo" {print $1}' /proc/devices)
echo $adcmajor
sudo rm -f /dev/spi_demo
sudo mknod /dev/spi_demo c $adcmajor 0
ls -l /dev/spi_demo
```

### 4.4 Test it (this is what you demo)
```bash
cd ~/Lab7
sudo python3 adc_watch.py
```
Output:
```
MCP3008 ADC stream (0..1023). Ctrl+C to stop.
ADC= 812 |###############################         |
ADC= 205 |########                                |   ← covered with your hand
```
Cover the sensor for a few seconds, then shine a phone flashlight on it for a few seconds.
✍️ **Write down** the range you see in the dark and in the light (for example dark 150–250, bright 750–850). You need these numbers for Item 4.
📸 **Screenshot** the values changing. ➡️ **Show the TA.** Ctrl+C to stop, but **keep `spi_demo` loaded** for Item 4.

---

## 5. Item 4: automatic lighting (20%)

### 5.1 Check that both drivers are loaded and both nodes exist
```bash
lsmod | grep -E '^(demo|spi_demo) '
ls -l /dev/demo /dev/spi_demo
```
If one is missing, redo 2.4 or 4.3. Make sure `adc_watch.py` is **not** still running.

### 5.2 Choose your thresholds
Take your numbers from step 4.4. Pick `low` a bit above your dark range and `high` a bit below your bright range.
Example: dark 150–250 and bright 750–850 → `--low 400 --high 600`.

How it works (hysteresis):
- ADC ≤ low → **DARK** → LED **ON**
- ADC ≥ high → **BRIGHT** → LED **OFF**
- in between → **keep** the previous state, so the LED doesn't flicker

### 5.3 Run it (this is what you demo)
```bash
cd ~/Lab7
sudo python3 auto_light.py --led LED1 --low 400 --high 600
```
If covering the sensor makes the number go **up** instead of down, add `--dark-high`.

Output:
```
AUTO LED1: low=400 high=600 dark-low; Ctrl+C stops and turns LED off
ADC= 812 band=HIGH mode=BRIGHT LED1=OFF readback=0
ADC= 210 band=LOW  mode=DARK   LED1=ON  readback=1     ← covered → LED1 on
ADC= 480 band=HOLD mode=DARK   LED1=ON  readback=1     ← in between → stays on
ADC= 790 band=HIGH mode=BRIGHT LED1=OFF readback=0     ← light → LED1 off
```
How the requirement is met: Python runs `./Lab7 LED1 on`, which opens `/dev/demo`. The driver's `write()` receives the command with `copy_from_user()` and writes the GPIO sysfs `value` file using kernel file I/O. `readback` is the status read back from the driver.

📸 **Screenshot and record a video** of covering and uncovering the sensor with the LED reacting. ➡️ **Show the TA.** Ctrl+C stops the program and turns the LED off.

### 5.4 Clean up when you're done
```bash
sudo rmmod spi_demo
sudo rmmod demo
sudo rm -f /dev/demo /dev/spi_demo
```

---

## 6. If something goes wrong

| Symptom | Fix |
|---|---|
| `make` errors about `SUBDIRS` | Our Makefile already uses `M=`. Make sure you're running the provided Makefile. |
| `make: *** missing separator` | The Makefile lines under `all:` and `clean:` must start with a real **Tab**. Re-copy the file. |
| `$'\r': command not found` or odd errors | The file got Windows line endings. Run `sed -i 's/\r$//' <file>`. |
| `insmod: File exists` | It's already loaded. Run `sudo rmmod demo` (or `spi_demo` / `button_irq`) and load it again. |
| `insmod: ... -16` / `Device or resource busy` / `gpio_request(481) failed: -16` | The pin is still exported in sysfs. `echo 481 \| sudo tee /sys/class/gpio/unexport` (use your pin number). |
| `open /dev/demo: No such file or directory` | You didn't run `mknod`. Redo step 2.4. |
| `open /dev/demo: No such device or address` | The node's major number is old. `sudo rm /dev/demo`, then redo `mknod` with the major from `/proc/devices`. |
| `Permission denied` | Run with `sudo`. |
| An LED never lights | Recheck the LED direction (long leg = +), the resistor, GND, and the pin from step 2.2. |
| Button prints nothing | Check step 3.2 (1/0 change?) and `/proc/interrupts`. If it shows 0→1 instead, load with `active_low=0`. |
| ADC always 0 or 1023 | Check the 3.3 V/GND on pins 16/15/14/9, DIN/DOUT not swapped, and CH0 at the divider midpoint. |
| ADC never goes above ~511 | DIN and DOUT are probably swapped, or VREF isn't 3.3 V. |
| LED does the opposite of what you want | Add or remove `--dark-high`. |
| LED never switches | Your ADC values never cross `--low` / `--high`. Pick thresholds from what you actually measured. |

---

## 7. Report and submission checklist

**Group report** (`Lab07_第X組_小組報告.pdf` / `.docx`):
1. 組別與組員名單: group number and member list (−20 if missing)
2. 實驗步驟截圖與說明: screenshots and an explanation for each Item 1–4 (−40 if missing). Include:
   - Item 1: build/insmod/mknod, `./Lab7` commands with `Status` output, `dmesg` with the 5 messages, LED photos
   - Item 2: the 1/0 sysfs check, `/proc/interrupts` before and after, `Button Pressed! Interrupt Triggered.` messages
   - Item 3: `insmod` + `/proc/devices` + `mknod`, ADC values in the dark and in the light
   - Item 4: the command with thresholds, the output switching DARK/BRIGHT, photos/video (YouTube link + screenshot)
   - Wiring photos plus a short explanation of each driver
3. 組員貢獻比例: contribution % per member, adding up to 100% (−20 if missing)

**Personal report** (`Lab07_第X組_<學號>_<姓名>_個人報告`): 個人心得 (your own reflection), submitted separately (−20 if missing).

**Zip**: a folder `Lab07_第X組` containing `Code/` (this Lab7 folder) and `Report/`, zipped as `Lab07_第X組.zip`.

Remember to register with the TA when you borrow and return the board.
