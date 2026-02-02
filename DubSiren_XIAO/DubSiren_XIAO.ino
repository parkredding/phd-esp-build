/*
 * DubSiren XIAO - Noisemaker Edition
 *
 * A compact, preset-based dub siren for the Seeed XIAO ESP32S3
 * Perfect for instant noise-making fun!
 *
 * Controls:
 * - 1 Rotary Encoder: Scroll through sound presets
 * - 1 Trigger Button: Hold to make sound
 * - 1 Modifier Button: Shifts to alternate preset bank
 *
 * Hardware: Seeed XIAO ESP32S3 (also works on Teyleten Supermini ESP32S3)
 * Audio: MAX98357A or PCM5102 I2S DAC
 */

#include <driver/i2s.h>
#include "DSP.h"

// ============================================================================
// PIN DEFINITIONS - XIAO ESP32S3
// ============================================================================

// I2S Audio Output to PCM5102 Purple Board
// PCM5102: VIN->3V3, GND->GND, SCK->GND, FMT->GND, XMT->3V3
#define I2S_BCLK        7     // Pin 12 / D8  / GPIO7  -> PCM5102 BCK
#define I2S_LRCK        8     // Pin 13 / D9  / GPIO8  -> PCM5102 LCK
#define I2S_DOUT        9     // Pin 14 / D10 / GPIO9  -> PCM5102 DIN

// Rotary Encoder (preset selection)
#define ENC_CLK         1     // Pin 1 / D0 / GPIO1
#define ENC_DT          2     // Pin 2 / D1 / GPIO2

// Buttons
#define BTN_TRIGGER     3     // Pin 3 / D2 / GPIO3 - Main trigger
#define BTN_MODIFIER    4     // Pin 4 / D3 / GPIO4 - Alt bank

// Status LED (optional - XIAO has built-in LED)
#define LED_STATUS      LED_BUILTIN

// ============================================================================
// I2S CONFIGURATION
// ============================================================================

#define I2S_PORT        I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_BUFFER_SIZE 128

// ============================================================================
// SOUND PRESETS
// ============================================================================

struct SoundPreset {
    const char* name;
    float baseFreq;
    float lfoRate;
    float lfoDepth;
    Waveform waveform;
    float filterCutoff;
    float filterRes;
    float delayTime;
    float delayFeedback;
    float delayMix;
    float releaseTime;
};

// Bank A - Classic Dub Sirens
const SoundPreset presetsA[] = {
    // name,        freq,  lfoRate, lfoDepth, wave,            cutoff, res,  delay, fb,   mix,  release
    {"Classic",     440,   3.0,     0.5,      Waveform::Square, 2500,  0.3,  0.25,  0.5,  0.3,  0.5},
    {"Slow Sweep",  330,   0.5,     0.7,      Waveform::Square, 1500,  0.5,  0.4,   0.6,  0.4,  1.0},
    {"Fast Alarm",  880,   8.0,     0.3,      Waveform::Square, 4000,  0.2,  0.1,   0.3,  0.2,  0.2},
    {"Deep Bass",   110,   1.5,     0.6,      Waveform::Saw,    800,   0.6,  0.3,   0.5,  0.3,  0.8},
    {"Sci-Fi",      660,   12.0,    0.4,      Waveform::Saw,    3000,  0.4,  0.15,  0.4,  0.25, 0.3},
    {"Police",      700,   4.0,     0.8,      Waveform::Square, 5000,  0.1,  0.0,   0.0,  0.0,  0.1},
    {"Submarine",   80,    0.3,     0.9,      Waveform::Sine,   400,   0.7,  0.5,   0.7,  0.5,  1.5},
    {"Laser",       1200,  20.0,    0.2,      Waveform::Saw,    6000,  0.3,  0.05,  0.2,  0.15, 0.15},
};

// Bank B - Experimental / Weird
const SoundPreset presetsB[] = {
    // name,        freq,  lfoRate, lfoDepth, wave,              cutoff, res,  delay, fb,   mix,  release
    {"Wobble",      200,   6.0,     0.8,      Waveform::Square,  1000,  0.7,  0.2,   0.6,  0.4,  0.6},
    {"Drone",       55,    0.1,     0.3,      Waveform::Saw,     600,   0.8,  0.45,  0.8,  0.5,  2.0},
    {"Glitch",      500,   15.0,    0.5,      Waveform::Square,  2000,  0.5,  0.08,  0.7,  0.35, 0.1},
    {"Haunted",     220,   0.7,     0.6,      Waveform::Triangle,900,   0.6,  0.5,   0.75, 0.45, 1.2},
    {"Robot",       350,   10.0,    0.4,      Waveform::Square,  1800,  0.4,  0.12,  0.5,  0.3,  0.25},
    {"UFO",         600,   25.0,    0.3,      Waveform::Sine,    4000,  0.2,  0.1,   0.4,  0.2,  0.2},
    {"Monster",     60,    2.0,     0.7,      Waveform::Saw,     500,   0.75, 0.35,  0.65, 0.4,  1.0},
    {"Chaos",       440,   30.0,    0.6,      Waveform::Saw,     3500,  0.5,  0.07,  0.8,  0.4,  0.3},
};

const int NUM_PRESETS = sizeof(presetsA) / sizeof(presetsA[0]);

// ============================================================================
// DSP OBJECTS
// ============================================================================

Oscillator osc;
LFO lfo;
Envelope env;
LowPassFilter filter;
DelayEffect delayFx;
DCBlocker dcBlock;

// ============================================================================
// STATE
// ============================================================================

int currentPreset = 0;
bool useAltBank = false;
bool triggered = false;
volatile bool triggerFlag = false;
volatile bool releaseFlag = false;

// Encoder state
int8_t encLastState = 0;

// Audio buffer
int16_t audioBuffer[I2S_BUFFER_SIZE * 2];

// ============================================================================
// APPLY PRESET
// ============================================================================

void applyPreset(int presetIndex) {
    const SoundPreset& p = useAltBank ? presetsB[presetIndex] : presetsA[presetIndex];

    osc.setFrequency(p.baseFreq);
    osc.setWaveform(p.waveform);
    lfo.setFrequency(p.lfoRate);
    lfo.setDepth(p.lfoDepth);
    filter.setCutoff(p.filterCutoff);
    filter.setResonance(p.filterRes);
    delayFx.setDelayTime(p.delayTime);
    delayFx.setFeedback(p.delayFeedback);
    delayFx.setDryWet(p.delayMix);
    env.setRelease(p.releaseTime);

    Serial.printf("Preset %d: %s\n", presetIndex, p.name);
}

// ============================================================================
// ENCODER READING
// ============================================================================

int readEncoder() {
    int change = 0;
    int clkState = digitalRead(ENC_CLK);

    if (clkState != encLastState) {
        if (digitalRead(ENC_DT) != clkState) {
            change = 1;
        } else {
            change = -1;
        }
    }
    encLastState = clkState;
    return change;
}

// ============================================================================
// BUTTON HANDLING
// ============================================================================

void IRAM_ATTR onTriggerChange() {
    if (digitalRead(BTN_TRIGGER) == LOW) {
        triggerFlag = true;
    } else {
        releaseFlag = true;
    }
}

void initButtons() {
    pinMode(BTN_TRIGGER, INPUT_PULLUP);
    pinMode(BTN_MODIFIER, INPUT_PULLUP);
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(BTN_TRIGGER), onTriggerChange, CHANGE);

    encLastState = digitalRead(ENC_CLK);
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
// CONTROLS UPDATE
// ============================================================================

void updateControls() {
    // Check modifier button for bank switch
    bool modPressed = !digitalRead(BTN_MODIFIER);
    if (modPressed != useAltBank) {
        useAltBank = modPressed;
        applyPreset(currentPreset);
        digitalWrite(LED_STATUS, useAltBank ? HIGH : LOW);
    }

    // Encoder for preset selection
    int change = readEncoder();
    if (change != 0) {
        currentPreset += change;
        if (currentPreset < 0) currentPreset = NUM_PRESETS - 1;
        if (currentPreset >= NUM_PRESETS) currentPreset = 0;
        applyPreset(currentPreset);
    }
}

// ============================================================================
// AUDIO GENERATION
// ============================================================================

void generateAudio() {
    // Handle trigger
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

    const SoundPreset& p = useAltBank ? presetsB[currentPreset] : presetsA[currentPreset];

    for (int i = 0; i < I2S_BUFFER_SIZE; i++) {
        // LFO modulation
        float lfoVal = lfo.generate();
        float modFreq = p.baseFreq * (1.0f + lfoVal * 0.5f);
        osc.setFrequency(modFreq);

        // Generate sound
        float sample = osc.generate();

        // Envelope
        float envVal = env.generate();
        sample *= envVal;

        // Filter
        sample = filter.process(sample);

        // Delay (if enabled for this preset)
        if (p.delayMix > 0.01f) {
            sample = delayFx.process(sample);
        }

        // DC blocking and soft clip
        sample = dcBlock.process(sample);
        sample = fastTanh(sample * 0.8f);

        // Convert to 16-bit stereo
        int16_t outSample = (int16_t)(sample * 32000.0f);
        audioBuffer[i * 2] = outSample;
        audioBuffer[i * 2 + 1] = outSample;
    }

    // Output to I2S
    size_t bytesWritten;
    i2s_write(I2S_PORT, audioBuffer, sizeof(audioBuffer), &bytesWritten, portMAX_DELAY);
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== DubSiren XIAO Noisemaker ===");
    Serial.println("Encoder: Change preset");
    Serial.println("Trigger: Hold to play");
    Serial.println("Modifier: Hold for Bank B\n");

    // LED
    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    // Initialize hardware
    initButtons();
    initI2S();

    // Load first preset
    env.setAttack(0.01f);
    applyPreset(0);

    Serial.println("Ready! Make some noise!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    updateControls();
    generateAudio();
}
