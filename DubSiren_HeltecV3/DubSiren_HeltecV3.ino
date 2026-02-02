/*
 * DubSiren - Heltec WiFi LoRa 32 V3 Full Build
 *
 * A complete dub siren synthesizer with:
 * - 5 rotary encoders (10 parameters via bank switching)
 * - 3 momentary buttons (trigger, shift, mode)
 * - Built-in OLED display for parameter feedback
 * - Full DSP: Oscillator, LFO, Filter, Delay, Reverb, Envelope
 *
 * Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3) - Meshtastic edition
 * Audio: PCM5102 Purple Board I2S DAC
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "DSP.h"

// ============================================================================
// PIN DEFINITIONS - Heltec WiFi LoRa 32 V3
// ============================================================================

// I2S Audio Output to PCM5102 Purple Board
// PCM5102: VIN->3V3, GND->GND, SCK->GND, FMT->GND, XMT->3V3
#define I2S_BCLK        47    // -> PCM5102 BCK
#define I2S_LRCK        48    // -> PCM5102 LCK
#define I2S_DOUT        26    // -> PCM5102 DIN

// Rotary Encoders - using available GPIO on Heltec V3
// Note: GPIO 17,18,21 reserved for OLED; GPIO 19 is VEXT
// Encoder 1: LFO Depth / LFO Rate (Bank B)
#define ENC1_CLK        6
#define ENC1_DT         7

// Encoder 2: Base Frequency / Delay Time (Bank B)
#define ENC2_CLK        5
#define ENC2_DT         4

// Encoder 3: Filter Cutoff / Filter Resonance (Bank B)
#define ENC3_CLK        3
#define ENC3_DT         2

// Encoder 4: Delay Feedback / Waveform Select (Bank B)
#define ENC4_CLK        1
#define ENC4_DT         38

// Encoder 5: Reverb Mix / Reverb Size (Bank B)
#define ENC5_CLK        39
#define ENC5_DT         40

// Buttons
#define BTN_TRIGGER     41    // Main trigger button
#define BTN_SHIFT       42    // Shift for Bank B access
#define BTN_MODE        46    // Mode/preset button

// OLED (Heltec built-in SSD1306 0.96")
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21

// ============================================================================
// OLED DISPLAY
// ============================================================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

// Display update timing
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_MS = 50;  // 20 FPS
bool displayNeedsUpdate = true;

// ============================================================================
// I2S CONFIGURATION
// ============================================================================

#define I2S_PORT        I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_BUFFER_SIZE 256

// ============================================================================
// DSP OBJECTS
// ============================================================================

Oscillator osc;
LFO lfo;
Envelope env;
LowPassFilter filter;
DelayEffect delayFx;
ReverbEffect reverbFx;
DCBlocker dcBlock;

// ============================================================================
// PARAMETER STATE
// ============================================================================

// Bank A parameters (direct encoder control)
float lfoDepth = 0.5f;
float baseFrequency = 440.0f;
float filterCutoff = 3000.0f;
float delayFeedback = 0.5f;
float reverbMix = 0.3f;

// Bank B parameters (shift + encoder)
float lfoRate = 2.0f;
float delayTime = 0.375f;
float filterResonance = 0.3f;
Waveform currentWaveform = Waveform::Square;
float reverbSize = 0.7f;
float releaseTime = 0.5f;

// State
bool shiftPressed = false;
bool triggered = false;
volatile bool triggerFlag = false;
volatile bool releaseFlag = false;

// Track which parameter changed for display
int lastChangedParam = -1;
unsigned long lastParamChangeTime = 0;

// Encoder state
int8_t encLastState[5] = {0};

// ============================================================================
// AUDIO BUFFER
// ============================================================================

int16_t audioBuffer[I2S_BUFFER_SIZE * 2];  // Stereo

// ============================================================================
// WAVEFORM NAMES
// ============================================================================

const char* waveformNames[] = {"SIN", "SQR", "SAW", "TRI"};

// ============================================================================
// ENCODER READING
// ============================================================================

struct EncoderPins {
    uint8_t clk;
    uint8_t dt;
};

const EncoderPins encoders[5] = {
    {ENC1_CLK, ENC1_DT},
    {ENC2_CLK, ENC2_DT},
    {ENC3_CLK, ENC3_DT},
    {ENC4_CLK, ENC4_DT},
    {ENC5_CLK, ENC5_DT}
};

void initEncoders() {
    for (int i = 0; i < 5; i++) {
        pinMode(encoders[i].clk, INPUT_PULLUP);
        pinMode(encoders[i].dt, INPUT_PULLUP);
        encLastState[i] = digitalRead(encoders[i].clk);
    }
}

int readEncoder(int idx) {
    int change = 0;
    int clkState = digitalRead(encoders[idx].clk);

    if (clkState != encLastState[idx]) {
        if (digitalRead(encoders[idx].dt) != clkState) {
            change = 1;
        } else {
            change = -1;
        }
    }
    encLastState[idx] = clkState;
    return change;
}

// ============================================================================
// BUTTON HANDLING
// ============================================================================

void IRAM_ATTR onTriggerPress() {
    triggerFlag = true;
}

void IRAM_ATTR onTriggerRelease() {
    releaseFlag = true;
}

void initButtons() {
    pinMode(BTN_TRIGGER, INPUT_PULLUP);
    pinMode(BTN_SHIFT, INPUT_PULLUP);
    pinMode(BTN_MODE, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(BTN_TRIGGER), onTriggerPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_TRIGGER), onTriggerRelease, RISING);
}

// ============================================================================
// I2S AUDIO SETUP
// ============================================================================

void initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = I2S_BUFFER_SIZE,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

// ============================================================================
// OLED DISPLAY
// ============================================================================

void initDisplay() {
    display.begin();
    display.setFont(u8g2_font_6x10_tf);
    display.clearBuffer();
    display.drawStr(20, 30, "DUB SIREN");
    display.drawStr(25, 45, "Heltec V3");
    display.sendBuffer();
    delay(1000);
}

void updateDisplay() {
    unsigned long now = millis();
    if (now - lastDisplayUpdate < DISPLAY_UPDATE_MS) return;
    lastDisplayUpdate = now;

    display.clearBuffer();

    // Title bar
    display.setFont(u8g2_font_6x10_tf);
    if (triggered) {
        display.drawBox(0, 0, 128, 12);
        display.setDrawColor(0);
        display.drawStr(4, 10, ">>> PLAYING <<<");
        display.setDrawColor(1);
    } else {
        display.drawStr(4, 10, shiftPressed ? "BANK B" : "BANK A");
        display.drawStr(80, 10, waveformNames[(int)currentWaveform]);
    }

    // Draw horizontal line
    display.drawHLine(0, 13, 128);

    // Parameters - two columns
    display.setFont(u8g2_font_5x7_tf);

    if (!shiftPressed) {
        // Bank A
        char buf[20];

        // Left column
        sprintf(buf, "LFO D: %d%%", (int)(lfoDepth * 100));
        display.drawStr(2, 24, buf);

        sprintf(buf, "FREQ: %dHz", (int)baseFrequency);
        display.drawStr(2, 34, buf);

        sprintf(buf, "FILT: %dHz", (int)filterCutoff);
        display.drawStr(2, 44, buf);

        // Right column
        sprintf(buf, "DLY FB: %d%%", (int)(delayFeedback * 100));
        display.drawStr(66, 24, buf);

        sprintf(buf, "VERB: %d%%", (int)(reverbMix * 100));
        display.drawStr(66, 34, buf);
    } else {
        // Bank B
        char buf[20];

        // Left column
        sprintf(buf, "LFO R: %.1fHz", lfoRate);
        display.drawStr(2, 24, buf);

        sprintf(buf, "DLY T: %dms", (int)(delayTime * 1000));
        display.drawStr(2, 34, buf);

        sprintf(buf, "FILT Q: %d%%", (int)(filterResonance * 100));
        display.drawStr(2, 44, buf);

        // Right column
        sprintf(buf, "WAVE: %s", waveformNames[(int)currentWaveform]);
        display.drawStr(66, 24, buf);

        sprintf(buf, "ROOM: %d%%", (int)(reverbSize * 100));
        display.drawStr(66, 34, buf);

        sprintf(buf, "REL: %.1fs", releaseTime);
        display.drawStr(66, 44, buf);
    }

    // Bottom bar - envelope level meter
    display.drawFrame(2, 54, 124, 8);
    int meterWidth = (int)(env.getValue() * 120);
    if (meterWidth > 0) {
        display.drawBox(4, 56, meterWidth, 4);
    }

    display.sendBuffer();
}

// ============================================================================
// PARAMETER UPDATE
// ============================================================================

void updateParameters() {
    shiftPressed = !digitalRead(BTN_SHIFT);

    for (int i = 0; i < 5; i++) {
        int change = readEncoder(i);
        if (change == 0) continue;

        displayNeedsUpdate = true;
        lastChangedParam = i + (shiftPressed ? 5 : 0);
        lastParamChangeTime = millis();

        float delta = change * 0.02f;  // Sensitivity

        if (!shiftPressed) {
            // Bank A
            switch (i) {
                case 0: lfoDepth = clampF(lfoDepth + delta, 0.0f, 1.0f); break;
                case 1: baseFrequency = clampF(baseFrequency + change * 10.0f, 50.0f, 2000.0f); break;
                case 2: filterCutoff = clampF(filterCutoff + change * 100.0f, 100.0f, 10000.0f); break;
                case 3: delayFeedback = clampF(delayFeedback + delta, 0.0f, 0.95f); break;
                case 4: reverbMix = clampF(reverbMix + delta, 0.0f, 1.0f); break;
            }
        } else {
            // Bank B
            switch (i) {
                case 0: lfoRate = clampF(lfoRate + delta * 5.0f, 0.1f, 20.0f); break;
                case 1: delayTime = clampF(delayTime + delta * 0.5f, 0.05f, 1.0f); break;
                case 2: filterResonance = clampF(filterResonance + delta, 0.0f, 0.95f); break;
                case 3:
                    // Cycle waveforms
                    if (change > 0) {
                        currentWaveform = (Waveform)(((int)currentWaveform + 1) % 4);
                    } else {
                        currentWaveform = (Waveform)(((int)currentWaveform + 3) % 4);
                    }
                    break;
                case 4: reverbSize = clampF(reverbSize + delta, 0.0f, 1.0f); break;
            }
        }
    }

    // Apply parameters to DSP
    lfo.setFrequency(lfoRate);
    lfo.setDepth(lfoDepth);
    osc.setWaveform(currentWaveform);
    filter.setCutoff(filterCutoff);
    filter.setResonance(filterResonance);
    delayFx.setDelayTime(delayTime);
    delayFx.setFeedback(delayFeedback);
    delayFx.setDryWet(0.5f);  // Fixed delay mix
    reverbFx.setRoomSize(reverbSize);
    reverbFx.setDryWet(reverbMix);
    env.setRelease(releaseTime);
}

// ============================================================================
// AUDIO GENERATION
// ============================================================================

void generateAudio() {
    // Handle trigger state changes
    if (triggerFlag) {
        triggerFlag = false;
        triggered = true;
        env.trigger();
        osc.resetPhase();
    }
    if (releaseFlag) {
        releaseFlag = false;
        triggered = false;
        env.release();
    }

    // Generate audio buffer
    for (int i = 0; i < I2S_BUFFER_SIZE; i++) {
        // LFO modulates oscillator frequency
        float lfoVal = lfo.generate();
        float modFreq = baseFrequency * (1.0f + lfoVal * 0.5f);
        osc.setFrequency(modFreq);

        // Generate oscillator
        float sample = osc.generate();

        // Apply envelope
        float envVal = env.generate();
        sample *= envVal;

        // Filter
        sample = filter.process(sample);

        // Delay
        sample = delayFx.process(sample);

        // Reverb
        sample = reverbFx.process(sample);

        // DC blocking
        sample = dcBlock.process(sample);

        // Soft clip output
        sample = fastTanh(sample * 0.8f);

        // Convert to 16-bit
        int16_t outSample = (int16_t)(sample * 32000.0f);

        // Stereo output
        audioBuffer[i * 2] = outSample;
        audioBuffer[i * 2 + 1] = outSample;
    }

    // Write to I2S
    size_t bytesWritten;
    i2s_write(I2S_PORT, audioBuffer, sizeof(audioBuffer), &bytesWritten, portMAX_DELAY);
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("DubSiren Heltec V3 - Full Build with OLED");

    // Initialize display first for visual feedback
    initDisplay();

    // Initialize hardware
    initEncoders();
    initButtons();
    initI2S();

    // Set initial DSP parameters
    osc.setFrequency(baseFrequency);
    osc.setWaveform(Waveform::Square);
    lfo.setFrequency(lfoRate);
    lfo.setDepth(lfoDepth);
    filter.setCutoff(filterCutoff);
    filter.setResonance(filterResonance);
    delayFx.setDelayTime(delayTime);
    delayFx.setFeedback(delayFeedback);
    delayFx.setDryWet(0.5f);
    reverbFx.setRoomSize(reverbSize);
    reverbFx.setDryWet(reverbMix);
    env.setAttack(0.01f);
    env.setRelease(releaseTime);

    Serial.println("Ready!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Update parameters from encoders
    updateParameters();

    // Generate and output audio
    generateAudio();

    // Update display (throttled internally)
    updateDisplay();
}
