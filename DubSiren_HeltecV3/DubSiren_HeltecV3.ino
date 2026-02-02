/*
 * DubSiren - Heltec WiFi LoRa 32 V3 Full Build
 *
 * A complete dub siren synthesizer with:
 * - 5 rotary encoders (10 parameters via bank switching)
 * - 3 momentary buttons (trigger, shift, mode)
 * - OLED display for parameter feedback
 * - Full DSP: Oscillator, LFO, Filter, Delay, Envelope
 *
 * Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3)
 * Audio: PCM5102 or MAX98357A I2S DAC
 */

#include <driver/i2s.h>
#include <Wire.h>
#include "DSP.h"

// ============================================================================
// PIN DEFINITIONS - Heltec V3
// ============================================================================

// I2S Audio Output (directly accessible pins on Heltec V3)
#define I2S_BCLK        26    // Bit clock
#define I2S_LRCK        25    // Word select / LR clock
#define I2S_DOUT        33    // Data out

// Rotary Encoders (directly accessible on Heltec V3)
// Encoder 1: LFO Depth / LFO Rate (Bank B)
#define ENC1_CLK        36
#define ENC1_DT         37

// Encoder 2: Base Frequency / Delay Time (Bank B)
#define ENC2_CLK        38
#define ENC2_DT         39

// Encoder 3: Filter Cutoff / Filter Resonance (Bank B)
#define ENC3_CLK        40
#define ENC3_DT         41

// Encoder 4: Delay Feedback / Waveform Select (Bank B)
#define ENC4_CLK        42
#define ENC4_DT         2

// Encoder 5: Dry/Wet Mix / Release Time (Bank B)
#define ENC5_CLK        1
#define ENC5_DT         3

// Buttons
#define BTN_TRIGGER     4     // Main trigger button
#define BTN_SHIFT       5     // Shift for Bank B access
#define BTN_MODE        6     // Mode/preset button

// OLED (Heltec built-in)
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21

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
DCBlocker dcBlock;

// ============================================================================
// PARAMETER STATE
// ============================================================================

// Bank A parameters (direct encoder control)
float lfoDepth = 0.5f;
float baseFrequency = 440.0f;
float filterCutoff = 3000.0f;
float delayFeedback = 0.5f;
float dryWetMix = 0.4f;

// Bank B parameters (shift + encoder)
float lfoRate = 2.0f;
float delayTime = 0.375f;
float filterResonance = 0.3f;
Waveform currentWaveform = Waveform::Square;
float releaseTime = 0.5f;

// State
bool shiftPressed = false;
bool triggered = false;
volatile bool triggerFlag = false;
volatile bool releaseFlag = false;

// Encoder state
int8_t encLastState[5] = {0};
int32_t encValues[5] = {0};

// ============================================================================
// AUDIO BUFFER
// ============================================================================

int16_t audioBuffer[I2S_BUFFER_SIZE * 2];  // Stereo

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
// PARAMETER UPDATE
// ============================================================================

void updateParameters() {
    shiftPressed = !digitalRead(BTN_SHIFT);

    for (int i = 0; i < 5; i++) {
        int change = readEncoder(i);
        if (change == 0) continue;

        float delta = change * 0.02f;  // Sensitivity

        if (!shiftPressed) {
            // Bank A
            switch (i) {
                case 0: lfoDepth = clampF(lfoDepth + delta, 0.0f, 1.0f); break;
                case 1: baseFrequency = clampF(baseFrequency + change * 10.0f, 50.0f, 2000.0f); break;
                case 2: filterCutoff = clampF(filterCutoff + change * 100.0f, 100.0f, 10000.0f); break;
                case 3: delayFeedback = clampF(delayFeedback + delta, 0.0f, 0.95f); break;
                case 4: dryWetMix = clampF(dryWetMix + delta, 0.0f, 1.0f); break;
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
                case 4: releaseTime = clampF(releaseTime + delta * 2.0f, 0.05f, 3.0f); break;
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
    delayFx.setDryWet(dryWetMix);
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
    Serial.println("DubSiren Heltec V3 - Full Build");

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
    delayFx.setDryWet(dryWetMix);
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
}
