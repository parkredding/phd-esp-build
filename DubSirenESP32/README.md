# Dub Siren ESP32 - Heltec WiFi LoRa 32 V3

A classic dub siren synthesizer running on the ESP32 Heltec V3 board with I2S audio output, OLED display, and button controls.

## Features

- **4 Waveforms**: Sine, Square (with anti-aliasing), Saw, Triangle
- **Pitch Envelope**: None, Up (rising pitch on release), Down (falling pitch on release)
- **Effects**: Low-pass filter with LFO modulation, Delay with feedback, Reverb
- **Real-time Audio**: 44.1kHz sample rate via I2S
- **OLED Display**: Shows current settings and status
- **Multiple Control Options**: Physical buttons and serial commands

---

## Hardware Requirements

### Required
- **Heltec WiFi LoRa 32 V3** (ESP32-S3 based)
- **I2S DAC Module** (PCM5102 recommended) or use internal DAC
- **Speaker/Amplifier** connected to DAC output
- **USB-C Cable** for programming and power

### Optional
- **2x Momentary Push Buttons** for waveform/pitch control
- **Potentiometers** for analog parameter control
- **3.5mm Audio Jack** for line output

---

## GPIO Pinout Diagram

### Heltec WiFi LoRa 32 V3 Board Layout

```
                    ┌──────────────────────────────────────┐
                    │         HELTEC WiFi LoRa 32 V3       │
                    │                                      │
                    │  ┌────────────────────────────────┐  │
                    │  │                                │  │
                    │  │         0.96" OLED             │  │
                    │  │         128 x 64               │  │
                    │  │        (Built-in)              │  │
                    │  │                                │  │
                    │  └────────────────────────────────┘  │
                    │                                      │
    ┌───────────────┤                                      ├───────────────┐
    │               │      [RST]           [PRG]           │               │
    │               │                                      │               │
    │   GPIO 0  ────┤ 0                                 Vext├──── Vext     │
    │   GPIO 1  ────┤ 1                                  GND├──── GND      │
    │   GPIO 2  ────┤ 2                                   36├──── GPIO 36  │
    │   GPIO 3  ────┤ 3                                   35├──── GPIO 35  │
    │   GPIO 4* ────┤ 4  ◄── I2S DATA                     48├──── GPIO 48* │
    │   GPIO 5* ────┤ 5  ◄── I2S BCK                      47├──── GPIO 47* │
    │   GPIO 6* ────┤ 6  ◄── I2S WS                       26├──── GPIO 26  │
    │   GPIO 7  ────┤ 7                                   33├──── GPIO 33  │
    │   GND     ────┤ GND                                 34├──── GPIO 34  │
    │   5V      ────┤ 5V                                  37├──── GPIO 37  │
    │   3V3     ────┤ 3V3                                 38├──── GPIO 38  │
    │   GPIO 19 ────┤ 19                                  39├──── GPIO 39  │
    │   GPIO 20 ────┤ 20                                  40├──── GPIO 40  │
    │   GPIO 21 ────┤ 21                                  41├──── GPIO 41  │
    │   GPIO 45 ────┤ 45                                  42├──── GPIO 42  │
    │   GPIO 46 ────┤ 46                                  GND├──── GND      │
    │               │                                      │               │
    └───────────────┤           [USB-C Port]               ├───────────────┘
                    │                                      │
                    └──────────────────────────────────────┘

    * = Used by this project
```

### Pin Assignments

| Function          | GPIO | Description                        |
|-------------------|------|------------------------------------|
| **I2S BCK**       | 5    | Bit Clock to DAC                   |
| **I2S WS**        | 6    | Word Select (LRCK) to DAC          |
| **I2S DATA**      | 4    | Audio Data to DAC                  |
| **Trigger Button**| 0    | PRG button (built-in, active LOW)  |
| **Waveform Button**| 47  | Cycle waveforms (optional)         |
| **Pitch Env Button**| 48 | Cycle pitch envelope (optional)    |

### Built-in OLED Display Pins (Internal - Do Not Use)

| Function    | GPIO | Note                    |
|-------------|------|-------------------------|
| OLED SDA    | 17   | I2C Data (internal)     |
| OLED SCL    | 18   | I2C Clock (internal)    |
| OLED RST    | 21   | Reset (internal)        |

---

## Wiring Diagrams

### PCM5102 DAC Module Connection

```
    HELTEC V3                         PCM5102 DAC MODULE
    ─────────                         ──────────────────

        GPIO 5  ──────────────────────►  BCK  (Bit Clock)

        GPIO 6  ──────────────────────►  LCK  (Word Select / LRCK)

        GPIO 4  ──────────────────────►  DIN  (Data In)

          3V3   ──────────────────────►  VCC  (3.3V Power)

          GND   ──────┬───────────────►  GND  (Ground)
                      │
                      ├───────────────►  SCK  (System Clock - tie to GND)
                      │
                      └───────────────►  FMT  (Format - tie to GND for I2S)

          3V3   ──────────────────────►  XMT  (Soft Mute - tie HIGH to unmute)


    PCM5102 AUDIO OUTPUT
    ────────────────────
                                         ┌─────────────────┐
        LOUT  ─────────────────────────► │  Left Speaker   │
                                         │  or Amplifier   │
        ROUT  ─────────────────────────► │  Right Speaker  │
                                         │  Input          │
        AGND  ─────────────────────────► │  Ground         │
                                         └─────────────────┘
```

### Complete Wiring Schematic

```
                                    ┌─────────────────────────────────────┐
                                    │         PCM5102 DAC Module          │
                                    │                                     │
                                    │  VCC ●────────────────┐             │
                                    │  GND ●──────────────┐ │             │
┌─────────────────────┐             │  BCK ●            │ │             │
│                     │             │  LCK ●            │ │             │
│   HELTEC V3         │             │  DIN ●            │ │ ┌─────────┐ │
│                     │             │  SCK ●            │ │ │         │ │
│              GPIO 5 ├─────────────┼──────┘            │ │ │  3.5mm  │ │
│              GPIO 6 ├─────────────┼──────┘            │ │ │  Jack   │ │
│              GPIO 4 ├─────────────┼──────┘            │ │ │   or    │ │
│                     │             │  FMT ●────────────┼─┤ │ Speaker │ │
│                 3V3 ├─────────────┼──────────────────►├─┘ │         │ │
│                     │             │  XMT ●────────────┘   └────┬────┘ │
│                 GND ├─────────────┼──────────────────►GND      │      │
│                     │             │                            │      │
│  ┌───────────────┐  │             │  LOUT ●───────────────────►│      │
│  │    OLED       │  │             │  ROUT ●───────────────────►│      │
│  │   Display     │  │             │  AGND ●───────────────────►GND    │
│  └───────────────┘  │             │                                   │
│                     │             └───────────────────────────────────┘
│  [PRG]  [RST]       │
│   │                 │
│   └── GPIO 0        │             ┌─────────────────────────────────────┐
│       (Trigger)     │             │      OPTIONAL BUTTON CONTROLS       │
│                     │             │                                     │
│             GPIO 47 ├─────────────┤  ┌─────┐                            │
│                     │             │  │ BTN │──► Waveform Cycle          │
│             GPIO 48 ├─────────────┤  └──┬──┘                            │
│                     │             │     │                               │
│                 GND ├─────────────┤  ┌──┴──┐                            │
│                     │             │  │ BTN │──► Pitch Envelope Cycle    │
└─────────────────────┘             │  └──┬──┘                            │
                                    │     │                               │
                                    │    GND                              │
                                    └─────────────────────────────────────┘
```

### Optional Button Wiring Detail

```
    GPIO 47 ────────┬────────────────── 3V3 (via internal pullup)
                    │
                    │    ┌───────┐
                    └────┤       │
                         │  SW1  │  Waveform Button
                    ┌────┤       │  (Normally Open)
                    │    └───────┘
                    │
    GND ────────────┘


    GPIO 48 ────────┬────────────────── 3V3 (via internal pullup)
                    │
                    │    ┌───────┐
                    └────┤       │
                         │  SW2  │  Pitch Envelope Button
                    ┌────┤       │  (Normally Open)
                    │    └───────┘
                    │
    GND ────────────┘
```

---

## Software Installation

### 1. Install Arduino IDE
Download and install [Arduino IDE 2.x](https://www.arduino.cc/en/software)

### 2. Add Heltec ESP32 Board Support

1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add this URL to "Additional Board Manager URLs":
   ```
   https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/releases/download/0.0.9/package_heltec_esp32_index.json
   ```
4. Go to **Tools → Board → Boards Manager**
5. Search for "Heltec" and install **Heltec ESP32 Series Dev-boards**

### 3. Select Board Settings

| Setting          | Value                    |
|------------------|--------------------------|
| Board            | WiFi LoRa 32 (V3)        |
| Upload Speed     | 921600                   |
| CPU Frequency    | 240MHz (WiFi)            |
| Flash Frequency  | 80MHz                    |
| Flash Mode       | QIO                      |
| Flash Size       | 8MB (64Mb)               |
| Partition Scheme | Default 4MB with spiffs  |
| PSRAM            | Disabled                 |

### 4. Upload the Sketch

1. Connect Heltec V3 via USB-C
2. Select the correct COM port in **Tools → Port**
3. Open `DubSirenESP32.ino`
4. Click **Upload**

---

## Usage

### Physical Controls

| Control               | Action                                      |
|-----------------------|---------------------------------------------|
| **Hold PRG button**   | Trigger siren (sound plays)                 |
| **Release PRG button**| Release siren (pitch envelope activates)    |
| **Press GPIO 47 btn** | Cycle waveform: Sine→Square→Saw→Triangle    |
| **Press GPIO 48 btn** | Cycle pitch env: None→Up→Down               |

### Serial Commands (115200 baud)

| Command | Action                          |
|---------|---------------------------------|
| `t`     | Trigger siren                   |
| `r`     | Release siren                   |
| `w`     | Cycle waveform                  |
| `p`     | Cycle pitch envelope            |
| `1`     | Set frequency to 220 Hz         |
| `2`     | Set frequency to 262 Hz (C4)    |
| `3`     | Set frequency to 330 Hz (E4)    |
| `4`     | Set frequency to 392 Hz (G4)    |
| `5`     | Set frequency to 440 Hz (A4)    |
| `6`     | Set frequency to 523 Hz (C5)    |
| `7`     | Set frequency to 659 Hz (E5)    |
| `8`     | Set frequency to 784 Hz (G5)    |
| `9`     | Set frequency to 880 Hz (A5)    |

### OLED Display

```
┌────────────────────────┐
│     DUB SIREN          │  ← Title
│                        │
│  Wave: SQUARE          │  ← Current waveform
│  Pitch: UP             │  ← Pitch envelope mode
│                        │
│  >>> ACTIVE <<<        │  ← Status (or "Press PRG to trigger")
└────────────────────────┘
```

---

## Sound Parameters

The synthesizer includes these DSP components:

### Oscillator
- 4 waveforms with anti-aliased square and saw waves
- Frequency range: 20 Hz - 20 kHz

### Amplitude Envelope
- Attack: 10ms (quick onset)
- Release: 500ms (adjustable via code)

### Pitch Envelope (on release)
- **None**: Pitch stays constant
- **Up**: Pitch rises 2 octaves during release
- **Down**: Pitch falls 2 octaves during release

### LFO (Low Frequency Oscillator)
- Rate: 2 Hz
- Modulates filter cutoff
- Triangle waveform

### Low-Pass Filter
- Cutoff: 3000 Hz base
- LFO modulation depth: ±3 octaves
- Resonance: 1.0

### Delay Effect
- Time: 300ms
- Feedback: 55%
- Mix: 30% wet

### Reverb
- Size: 70%
- Mix: 30% wet
- Damping: 50%

---

## Customization

### Changing Default Parameters

Edit these values in `DubSirenESP32.ino`:

```cpp
// In AudioEngine constructor:
audioEngine.setFrequency(440.0f);        // Base frequency
audioEngine.setWaveform(Waveform::Square);
audioEngine.setLfoRate(2.0f);            // LFO speed in Hz
audioEngine.setLfoDepth(0.5f);           // LFO intensity
audioEngine.setFilterCutoff(3000.0f);    // Filter cutoff Hz
audioEngine.setDelayTime(0.3f);          // Delay in seconds
audioEngine.setDelayFeedback(0.55f);     // 0.0 - 0.95
audioEngine.setDelayMix(0.3f);           // Dry/wet mix
audioEngine.setReverbSize(0.7f);         // Room size
audioEngine.setReverbMix(0.3f);          // Dry/wet mix
audioEngine.setReleaseTime(0.5f);        // Release in seconds
```

### Changing Pin Assignments

```cpp
// I2S pins
#define I2S_BCK_PIN       5
#define I2S_WS_PIN        6
#define I2S_DATA_PIN      4

// Button pins
#define TRIGGER_BTN_PIN   0     // PRG button
#define WAVEFORM_BTN_PIN  47
#define PITCHENV_BTN_PIN  48
```

### Adding Potentiometer Control

To add analog control (e.g., for frequency):

```cpp
// In setup():
pinMode(1, INPUT);  // Use GPIO 1 for potentiometer

// In loop():
int potValue = analogRead(1);
float frequency = map(potValue, 0, 4095, 100, 1000);
audioEngine.setFrequency(frequency);
```

---

## Troubleshooting

### No Sound

1. **Check DAC wiring** - Verify BCK, WS, DATA connections
2. **Check DAC power** - Ensure VCC is 3.3V, not 5V
3. **Check XMT pin** - Must be tied HIGH to unmute
4. **Check SCK pin** - Should be tied to GND
5. **Check speaker/amp** - Test with known working audio source

### Distorted Sound

1. **Reduce volume** - Lower the volume parameter in code
2. **Check power supply** - Use quality USB power source
3. **Add capacitors** - 100µF on DAC VCC, 10µF on audio output

### Display Not Working

1. **Reset the board** - Press RST button
2. **Check Heltec library** - Ensure correct version is installed
3. **Verify board selection** - Must be "WiFi LoRa 32 (V3)"

### Button Not Responding

1. **Check wiring** - Button should connect GPIO to GND
2. **Internal pullups** - Code enables INPUT_PULLUP
3. **Debounce timing** - Increase `debounceDelay` if needed

### Compilation Errors

1. **Install Heltec library** - Follow board installation steps
2. **Select correct board** - "WiFi LoRa 32 (V3)"
3. **Update Arduino IDE** - Use version 2.x

---

## Memory Usage

| Resource        | Used      | Available | Usage |
|-----------------|-----------|-----------|-------|
| Flash           | ~450 KB   | 4 MB      | 11%   |
| RAM             | ~180 KB   | 320 KB    | 56%   |
| Delay Buffer    | 88 KB     | -         | -     |
| Reverb Buffer   | 16 KB     | -         | -     |
| Audio Buffer    | 1 KB      | -         | -     |

---

## Technical Specifications

| Parameter       | Value                          |
|-----------------|--------------------------------|
| Sample Rate     | 44,100 Hz                      |
| Bit Depth       | 16-bit                         |
| Channels        | Stereo (mono signal)           |
| Buffer Size     | 256 samples                    |
| Latency         | ~6ms                           |
| CPU Core        | Audio on Core 1, UI on Core 0  |

---

## License

This project is open source. See LICENSE file for details.

---

## Credits

Converted from the Raspberry Pi Dub Siren project for ESP32 Heltec V3.
