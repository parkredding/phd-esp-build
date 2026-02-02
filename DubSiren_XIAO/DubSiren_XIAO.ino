/*
 * DubSiren XIAO - NJD Edition
 *
 * A hands-on dub siren for the Seeed XIAO ESP32S3
 * Two encoders for real-time control, button to cycle presets
 *
 * Controls:
 * - Encoder 1 (D0/D1): Pitch control
 * - Encoder 2 (D4/D5): LFO Rate control
 * - Preset Button (D3): Tap to cycle through NJD presets
 * - Trigger Button (D2): Hold to make sound
 *
 * Hardware: Seeed XIAO ESP32S3 (also works on Teyleten Supermini ESP32S3)
 * Audio: PCM5102 Purple Board I2S DAC
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

// Encoder 1 - Pitch Control
#define ENC1_CLK        1     // Pin 1 / D0 / GPIO1
#define ENC1_DT         2     // Pin 2 / D1 / GPIO2

// Encoder 2 - LFO Rate Control
#define ENC2_CLK        5     // Pin 5 / D4 / GPIO5
#define ENC2_DT         6     // Pin 6 / D5 / GPIO6

// Buttons
#define BTN_TRIGGER     3     // Pin 3 / D2 / GPIO3 - Hold to play
#define BTN_PRESET      4     // Pin 4 / D3 / GPIO4 - Tap to cycle preset

// Status LED
#define LED_STATUS      LED_BUILTIN

// ============================================================================
// I2S CONFIGURATION
// ============================================================================

#define I2S_PORT        I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_BUFFER_SIZE 128

// ============================================================================
// NJD SOUND PRESETS - Classic Dub Siren Tones
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

// NJD Dub Presets - Roots, Steppers, and Heavyweight sounds
const SoundPreset njdPresets[] = {
    // name,            freq,  lfoRate, lfoDepth, wave,              cutoff, res,  delay, fb,   mix,  release
    {"Roots Classic",   440,   2.5,     0.5,      Waveform::Square,  2000,   0.4,  0.375, 0.55, 0.35, 0.6},
    {"Steppers",        380,   4.0,     0.6,      Waveform::Square,  2500,   0.3,  0.25,  0.5,  0.3,  0.4},
    {"King Tubby",      330,   1.5,     0.7,      Waveform::Square,  1200,   0.6,  0.5,   0.7,  0.45, 1.0},
    {"Scientist",       520,   3.5,     0.45,     Waveform::Square,  1800,   0.5,  0.333, 0.6,  0.4,  0.5},
    {"Mad Professor",   280,   2.0,     0.8,      Waveform::Saw,     1000,   0.7,  0.45,  0.65, 0.5,  0.8},
    {"Iration",         660,   5.0,     0.4,      Waveform::Square,  3000,   0.25, 0.2,   0.45, 0.25, 0.3},
    {"Channel One",     220,   1.0,     0.6,      Waveform::Square,  800,    0.65, 0.5,   0.75, 0.5,  1.2},
    {"Heavyweight",     165,   0.8,     0.9,      Waveform::Saw,     600,    0.8,  0.45,  0.8,  0.55, 1.5},
};

const int NUM_PRESETS = sizeof(njdPresets) / sizeof(njdPresets[0]);

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
bool triggered = false;
volatile bool triggerFlag = false;
volatile bool releaseFlag = false;

// Live parameters (modified by encoders)
float livePitch = 440.0f;
float liveLfoRate = 2.5f;

// Encoder states
int8_t enc1LastState = 0;
int8_t enc2LastState = 0;

// Preset button debounce
unsigned long lastPresetPress = 0;
const unsigned long DEBOUNCE_MS = 200;

// Audio buffer
int16_t audioBuffer[I2S_BUFFER_SIZE * 2];

// ============================================================================
// APPLY PRESET
// ============================================================================

void applyPreset(int presetIndex) {
    const SoundPreset& p = njdPresets[presetIndex];

    // Set live parameters from preset (can be tweaked with encoders)
    livePitch = p.baseFreq;
    liveLfoRate = p.lfoRate;

    // Apply fixed preset parameters
    osc.setFrequency(livePitch);
    osc.setWaveform(p.waveform);
    lfo.setFrequency(liveLfoRate);
    lfo.setDepth(p.lfoDepth);
    filter.setCutoff(p.filterCutoff);
    filter.setResonance(p.filterRes);
    delayFx.setDelayTime(p.delayTime);
    delayFx.setFeedback(p.delayFeedback);
    delayFx.setDryWet(p.delayMix);
    env.setRelease(p.releaseTime);

    Serial.printf("NJD Preset %d: %s (%.0fHz, %.1fHz LFO)\n",
                  presetIndex, p.name, p.baseFreq, p.lfoRate);
}

// ============================================================================
// ENCODER READING
// ============================================================================

int readEncoder(int clkPin, int dtPin, int8_t& lastState) {
    int change = 0;
    int clkState = digitalRead(clkPin);

    if (clkState != lastState) {
        if (digitalRead(dtPin) != clkState) {
            change = 1;
        } else {
            change = -1;
        }
    }
    lastState = clkState;
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

void initHardware() {
    // Encoder 1 pins
    pinMode(ENC1_CLK, INPUT_PULLUP);
    pinMode(ENC1_DT, INPUT_PULLUP);

    // Encoder 2 pins
    pinMode(ENC2_CLK, INPUT_PULLUP);
    pinMode(ENC2_DT, INPUT_PULLUP);

    // Button pins
    pinMode(BTN_TRIGGER, INPUT_PULLUP);
    pinMode(BTN_PRESET, INPUT_PULLUP);

    // Trigger interrupt
    attachInterrupt(digitalPinToInterrupt(BTN_TRIGGER), onTriggerChange, CHANGE);

    // Initialize encoder states
    enc1LastState = digitalRead(ENC1_CLK);
    enc2LastState = digitalRead(ENC2_CLK);
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
    // Preset button - cycle through presets on press
    if (digitalRead(BTN_PRESET) == LOW) {
        unsigned long now = millis();
        if (now - lastPresetPress > DEBOUNCE_MS) {
            lastPresetPress = now;
            currentPreset = (currentPreset + 1) % NUM_PRESETS;
            applyPreset(currentPreset);

            // Flash LED
            digitalWrite(LED_STATUS, HIGH);
            delay(50);
            digitalWrite(LED_STATUS, LOW);
        }
    }

    // Encoder 1 - Pitch control
    int pitchChange = readEncoder(ENC1_CLK, ENC1_DT, enc1LastState);
    if (pitchChange != 0) {
        // Logarithmic pitch scaling for musical feel
        float pitchMultiplier = (pitchChange > 0) ? 1.02f : 0.98f;
        livePitch = clampF(livePitch * pitchMultiplier, 50.0f, 2000.0f);
        Serial.printf("Pitch: %.1f Hz\n", livePitch);
    }

    // Encoder 2 - LFO Rate control
    int lfoChange = readEncoder(ENC2_CLK, ENC2_DT, enc2LastState);
    if (lfoChange != 0) {
        liveLfoRate = clampF(liveLfoRate + (lfoChange * 0.2f), 0.1f, 30.0f);
        lfo.setFrequency(liveLfoRate);
        Serial.printf("LFO Rate: %.1f Hz\n", liveLfoRate);
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

    const SoundPreset& p = njdPresets[currentPreset];

    for (int i = 0; i < I2S_BUFFER_SIZE; i++) {
        // LFO modulation using live pitch
        float lfoVal = lfo.generate();
        float modFreq = livePitch * (1.0f + lfoVal * p.lfoDepth);
        osc.setFrequency(modFreq);

        // Generate sound
        float sample = osc.generate();

        // Envelope
        float envVal = env.generate();
        sample *= envVal;

        // Filter
        sample = filter.process(sample);

        // Delay
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
    Serial.println("\n========================================");
    Serial.println("    DubSiren XIAO - NJD Edition");
    Serial.println("========================================");
    Serial.println("Encoder 1 (D0/D1): Pitch");
    Serial.println("Encoder 2 (D4/D5): LFO Rate");
    Serial.println("Button (D3): Cycle Preset");
    Serial.println("Button (D2): TRIGGER - Hold to play");
    Serial.println("----------------------------------------\n");

    // LED
    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    // Initialize hardware
    initHardware();
    initI2S();

    // Load first preset
    env.setAttack(0.01f);
    applyPreset(0);

    Serial.println("\nReady! Hold trigger and twist the knobs!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    updateControls();
    generateAudio();
}
