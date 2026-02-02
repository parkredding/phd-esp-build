# DubSiren ESP32 Wiring Guide

## PCM5102 Purple Board Setup

Before wiring audio, configure the PCM5102 board jumpers/pads:

| PCM5102 Pin | Connect To | Purpose |
|-------------|------------|---------|
| SCK | GND | Use internal clock |
| FMT | GND | I2S standard format |
| XMT | 3V3 | Disable soft mute |
| FLT | (leave open) | Normal latency filter |
| DMP | (leave open) | Normal de-emphasis |

---

## XIAO ESP32S3 Wiring

### Board Orientation
```
        [USB-C]
    ┌─────────────┐
  1 │ D0      D10 │ 14
  2 │ D1       D9 │ 13
  3 │ D2       D8 │ 12
  4 │ D3       D7 │ 11
  5 │ D4      3V3 │ 10
  6 │ D5      GND │ 9
  7 │ D6       5V │ 8
    └─────────────┘
```

### XIAO Pin Reference
| Physical Pin | Silkscreen | GPIO | Function in DubSiren |
|--------------|------------|------|----------------------|
| 1 | D0 | GPIO1 | Encoder 1 CLK (Pitch) |
| 2 | D1 | GPIO2 | Encoder 1 DT (Pitch) |
| 3 | D2 | GPIO3 | Trigger Button (hold to play) |
| 4 | D3 | GPIO4 | Preset Button (tap to cycle) |
| 5 | D4 | GPIO5 | Encoder 2 CLK (LFO Rate) |
| 6 | D5 | GPIO6 | Encoder 2 DT (LFO Rate) |
| 7 | D6 | GPIO43 | (available) |
| 8 | 5V | — | (power input) |
| 9 | GND | — | Ground |
| 10 | 3V3 | — | 3.3V Power |
| 11 | D7 | GPIO44 | (available) |
| 12 | D8 | GPIO7 | I2S BCK |
| 13 | D9 | GPIO8 | I2S LCK |
| 14 | D10 | GPIO9 | I2S DIN |

### PCM5102 to XIAO Wiring

```
PCM5102 Purple Board          XIAO ESP32S3
────────────────────          ────────────
VIN  ●────────────────────────● 3V3  (Pin 10)
GND  ●────────────────────────● GND  (Pin 9)
BCK  ●────────────────────────● D8   (Pin 12, GPIO7)
LCK  ●────────────────────────● D9   (Pin 13, GPIO8)
DIN  ●────────────────────────● D10  (Pin 14, GPIO9)
SCK  ●────────────────────────● GND  (Pin 9)
FMT  ●────────────────────────● GND  (Pin 9)
XMT  ●────────────────────────● 3V3  (Pin 10)
```

### Control Wiring (XIAO NJD Edition)

**Encoder 1 - Pitch Control (KY-040 or similar)**
```
Encoder 1                     XIAO ESP32S3
─────────                     ────────────
GND  ●────────────────────────● GND  (Pin 9)
+    ●────────────────────────● 3V3  (Pin 10)
CLK  ●────────────────────────● D0   (Pin 1, GPIO1)
DT   ●────────────────────────● D1   (Pin 2, GPIO2)
SW   ●──── (not used) ────────
```

**Encoder 2 - LFO Rate Control**
```
Encoder 2                     XIAO ESP32S3
─────────                     ────────────
GND  ●────────────────────────● GND  (Pin 9)
+    ●────────────────────────● 3V3  (Pin 10)
CLK  ●────────────────────────● D4   (Pin 5, GPIO5)
DT   ●────────────────────────● D5   (Pin 6, GPIO6)
SW   ●──── (not used) ────────
```

**Buttons (active low with internal pullup)**
```
Trigger Button (HOLD TO PLAY) XIAO ESP32S3
───────────────────────────── ────────────
One leg  ●────────────────────● D2   (Pin 3, GPIO3)
Other leg ●───────────────────● GND  (Pin 9)

Preset Button (TAP TO CYCLE)  XIAO ESP32S3
───────────────────────────── ────────────
One leg  ●────────────────────● D3   (Pin 4, GPIO4)
Other leg ●───────────────────● GND  (Pin 9)
```

---

## Heltec WiFi LoRa 32 V3 Wiring

### Board Orientation (component side up, USB at bottom)
```
              [Antenna]
         ┌────────────────┐
         │   [OLED 0.96"] │
         │                │
    GND  │●              ●│ GND
    3V3  │●              ●│ 3V3
   GPIO1 │●              ●│ GPIO48
   GPIO2 │●              ●│ GPIO47
   GPIO3 │●              ●│ GPIO26
   GPIO4 │●              ●│ GPIO46
   GPIO5 │●              ●│ GPIO45
   GPIO6 │●              ●│ GPIO42
   GPIO7 │●              ●│ GPIO41
  GPIO19 │●              ●│ GPIO40
  GPIO20 │●              ●│ GPIO39
  GPIO21 │●              ●│ GPIO38
         │    [USB-C]     │
         └────────────────┘

Note: GPIO17/18/21 used by OLED, GPIO19 is VEXT power control
```

### PCM5102 to Heltec V3 Wiring

```
PCM5102 Purple Board          Heltec V3
────────────────────          ─────────
VIN  ●────────────────────────● 3V3
GND  ●────────────────────────● GND
BCK  ●────────────────────────● GPIO47 (right side)
LCK  ●────────────────────────● GPIO48 (right side)
DIN  ●────────────────────────● GPIO26 (right side)
SCK  ●────────────────────────● GND
FMT  ●────────────────────────● GND
XMT  ●────────────────────────● 3V3
```

### Encoder Wiring (Heltec V3 - 5 encoders)

| Encoder | Function (Bank A / B) | CLK Pin | DT Pin |
|---------|----------------------|---------|--------|
| 1 | LFO Depth / LFO Rate | GPIO6 | GPIO7 |
| 2 | Base Freq / Delay Time | GPIO5 | GPIO4 |
| 3 | Filter Cutoff / Resonance | GPIO3 | GPIO2 |
| 4 | Delay FB / Waveform | GPIO1 | GPIO38 |
| 5 | Dry/Wet / Release | GPIO39 | GPIO40 |

All encoder GND pins → GND
All encoder + pins → 3V3

### Button Wiring (Heltec V3)

| Button | Function | GPIO Pin |
|--------|----------|----------|
| Trigger | Play sound | GPIO41 |
| Shift | Access Bank B | GPIO42 |
| Mode | (reserved) | GPIO46 |

All button common → GND (uses internal pullup)

---

## Schematic Notes

### Power
- Both boards run on 3.3V logic
- PCM5102 VIN accepts 3.3V (some boards also accept 5V, check yours)
- XIAO can be powered via USB-C or 5V pin
- Heltec can be powered via USB-C or battery connector

### Audio Output
- PCM5102 has 3.5mm jack and/or header pins for L/R audio out
- Connect to powered speakers or amplifier
- Output is line level (~1V RMS)

### Encoder Notes
- Use encoders with built-in pull-ups, or enable internal pullups
- Code uses internal INPUT_PULLUP on all encoder/button pins
- Debouncing is handled in software

---

## Quick Test

After wiring, upload the sketch and:

1. **XIAO**: Turn encoder to change presets (serial monitor shows preset names)
2. **XIAO**: Hold trigger button - you should hear sound from PCM5102
3. **Heltec**: Same, plus shift button accesses Bank B parameters
