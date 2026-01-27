/**
 * Dub Siren ESP32 - Heltec WiFi LoRa 32 V4
 *
 * A dub siren synthesizer for ESP32 with I2S audio output.
 * Designed for Heltec V4 board with built-in OLED display.
 *
 * Features:
 * - Real-time oscilloscope display (auto-activates after 1s idle)
 * - Debug log display on OLED for troubleshooting
 * - Multiple waveforms with anti-aliasing
 * - Delay and reverb effects
 *
 * Hardware:
 * - Heltec WiFi LoRa 32 V4 (ESP32-S3)
 * - External I2S DAC (PCM5102 recommended) or internal DAC
 * - Optional: trigger button, potentiometers
 *
 * I2S Wiring (PCM5102):
 * - BCK  -> GPIO 5
 * - WS   -> GPIO 6 (LRCK)
 * - DATA -> GPIO 4
 * - VCC  -> 3.3V
 * - GND  -> GND
 * - SCK  -> GND (uses internal clock)
 * - FMT  -> GND (I2S format)
 * - XMT  -> 3.3V (unmute)
 *
 * Controls:
 * - PRG Button (GPIO 0): Trigger/Release siren
 * - GPIO 47: Cycle waveform (optional external button)
 * - GPIO 48: Cycle pitch envelope mode (optional external)
 *
 * Display Modes:
 * - Normal: Shows waveform, pitch envelope, status
 * - Oscilloscope: Auto-shows after 1s idle, displays live waveform
 * - Debug: Shows log messages for 3s when errors occur
 */

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <U8g2lib.h>  // Install "U8g2" from Library Manager

// ============================================================================
// Configuration
// ============================================================================

// Audio settings
#define SAMPLE_RATE       44100
#define BUFFER_SIZE       256
#define I2S_NUM           I2S_NUM_0

// I2S pins for Heltec V4
#define I2S_BCK_PIN       5
#define I2S_WS_PIN        6
#define I2S_DATA_PIN      4

// Button pins (Heltec V4)
#define TRIGGER_BTN_PIN   0     // PRG button (directly on board)
#define WAVEFORM_BTN_PIN  47    // External button
#define PITCHENV_BTN_PIN  48    // External button

// OLED pins (Heltec V4 built-in SSD1306 128x64)
#define OLED_SDA          17
#define OLED_SCL          18
#define OLED_RST          21
#define VEXT_PIN          36    // Vext control - must be LOW to power OLED

// Initialize OLED display (SSD1306 128x64 I2C)
// Software I2C with explicit pin assignment for Heltec V4
// Constructor: U8G2_SSD1306_128X64_NONAME_F_SW_I2C(rotation, clock, data, reset)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ OLED_SCL, /* data=*/ OLED_SDA, /* reset=*/ U8X8_PIN_NONE);

// ============================================================================
// DSP Constants
// ============================================================================

constexpr float PI_F = 3.14159265358979323846f;
constexpr float TWO_PI_F = 2.0f * PI_F;
constexpr float MAX_SAFE_AMPLITUDE = 10.0f;

// ============================================================================
// Waveform and Envelope Types
// ============================================================================

enum class Waveform {
    Sine = 0,
    Square = 1,
    Saw = 2,
    Triangle = 3
};

enum class PitchEnvelopeMode {
    None = 0,
    Up = 1,
    Down = 2
};

// ============================================================================
// Utility Functions
// ============================================================================

inline float clampF(float value, float minVal, float maxVal) {
    return max(minVal, min(maxVal, value));
}

inline float clampSample(float value) {
    if (value > MAX_SAFE_AMPLITUDE) return MAX_SAFE_AMPLITUDE;
    if (value < -MAX_SAFE_AMPLITUDE) return -MAX_SAFE_AMPLITUDE;
    return value;
}

inline float lerpF(float a, float b, float t) {
    return a + t * (b - a);
}

inline float fastTanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ============================================================================
// SmoothedValue Class
// ============================================================================

class SmoothedValue {
public:
    SmoothedValue(float initialValue = 0.0f, float smoothingCoeff = 0.01f)
        : target(initialValue), current(initialValue), coeff(smoothingCoeff) {}

    void setTarget(float newTarget) { target = newTarget; }
    void setImmediate(float value) { target = value; current = value; }

    float getNext() {
        current += (target - current) * coeff;
        return current;
    }

    float getCurrent() const { return current; }
    float getTarget() const { return target; }

private:
    float target;
    float current;
    float coeff;
};

// ============================================================================
// Oscillator Class
// ============================================================================

class Oscillator {
public:
    Oscillator(int sr = SAMPLE_RATE)
        : sampleRate(sr), frequency(440.0f), phase(0.0f), waveform(Waveform::Sine) {}

    float generateSample() {
        float sample = 0.0f;
        float dt = frequency / (float)sampleRate;

        switch (waveform) {
            case Waveform::Sine:
                sample = sinf(TWO_PI_F * phase);
                break;
            case Waveform::Square:
                sample = generateSquarePolyBlep(dt);
                break;
            case Waveform::Saw:
                sample = generateSawPolyBlep(dt);
                break;
            case Waveform::Triangle:
                sample = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
                break;
        }

        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;

        return sample;
    }

    void setFrequency(float freq) { frequency = clampF(freq, 20.0f, 20000.0f); }
    void setWaveform(Waveform wf) { waveform = wf; }
    void resetPhase() { phase = 0.0f; }
    float getFrequency() const { return frequency; }
    Waveform getWaveform() const { return waveform; }

private:
    int sampleRate;
    float frequency;
    float phase;
    Waveform waveform;

    float polyBlep(float t, float dt) const {
        if (t < dt) {
            float tNorm = t / dt;
            return tNorm + tNorm - tNorm * tNorm - 1.0f;
        } else if (t > 1.0f - dt) {
            float tNorm = (t - 1.0f) / dt;
            return tNorm * tNorm + tNorm + tNorm + 1.0f;
        }
        return 0.0f;
    }

    float generateSquarePolyBlep(float dt) {
        float value = (phase < 0.5f) ? 1.0f : -1.0f;
        value += 2.0f * polyBlep(phase, dt);
        float phaseShifted = phase + 0.5f;
        if (phaseShifted >= 1.0f) phaseShifted -= 1.0f;
        value -= 2.0f * polyBlep(phaseShifted, dt);
        return value;
    }

    float generateSawPolyBlep(float dt) {
        float value = 2.0f * phase - 1.0f;
        value -= 2.0f * polyBlep(phase, dt);
        return value;
    }
};

// ============================================================================
// Envelope Class
// ============================================================================

class Envelope {
public:
    Envelope(int sr = SAMPLE_RATE)
        : sampleRate(sr), attackTime(0.01f), releaseTime(0.5f),
          currentValue(0.0f), active(false) {
        updateCoefficients();
    }

    float generateSample() {
        float target = active ? 1.0f : 0.0f;
        float coeff = active ? attackCoeff : releaseCoeff;
        currentValue += (target - currentValue) * coeff;
        return currentValue;
    }

    void trigger() { active = true; }
    void release() { active = false; }

    void setAttack(float timeSeconds) {
        attackTime = clampF(timeSeconds, 0.001f, 2.0f);
        updateCoefficients();
    }

    void setRelease(float timeSeconds) {
        releaseTime = clampF(timeSeconds, 0.01f, 5.0f);
        updateCoefficients();
    }

    float getCurrentValue() const { return currentValue; }
    bool isActive() const { return active || currentValue > 0.001f; }

private:
    int sampleRate;
    float attackTime;
    float releaseTime;
    float attackCoeff;
    float releaseCoeff;
    float currentValue;
    bool active;

    void updateCoefficients() {
        const float DECAY_SCALE = 4.605f;
        attackCoeff = DECAY_SCALE / (attackTime * (float)sampleRate);
        releaseCoeff = DECAY_SCALE / (releaseTime * (float)sampleRate);
    }
};

// ============================================================================
// LFO Class
// ============================================================================

class LFO {
public:
    LFO(int sr = SAMPLE_RATE)
        : sampleRate(sr), frequency(5.0f), phase(0.0f),
          waveform(Waveform::Triangle), depth(0.5f) {}

    float generateSample() {
        float value = 0.0f;
        float dt = frequency / (float)sampleRate;

        switch (waveform) {
            case Waveform::Sine:
                value = sinf(TWO_PI_F * phase);
                break;
            case Waveform::Square:
                value = (phase < 0.5f) ? 1.0f : -1.0f;
                break;
            case Waveform::Saw:
                value = 2.0f * phase - 1.0f;
                break;
            case Waveform::Triangle:
                value = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
                break;
        }

        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;

        return value * depth;
    }

    void setFrequency(float freq) { frequency = clampF(freq, 0.1f, 20.0f); }
    void setWaveform(Waveform wf) { waveform = wf; }
    void setDepth(float d) { depth = clampF(d, 0.0f, 1.0f); }

private:
    int sampleRate;
    float frequency;
    float phase;
    Waveform waveform;
    float depth;
};

// ============================================================================
// Low-Pass Filter Class
// ============================================================================

class LowPassFilter {
public:
    LowPassFilter(int sr = SAMPLE_RATE)
        : sampleRate(sr), cutoff(3000.0f), cutoffCurrent(3000.0f),
          resonance(1.0f), resonanceCurrent(1.0f), prevOutput(0.0f), smoothing(0.05f) {}

    float processSample(float input) {
        cutoffCurrent += (cutoff - cutoffCurrent) * smoothing;
        resonanceCurrent += (resonance - resonanceCurrent) * smoothing;

        float rc = 1.0f / (TWO_PI_F * cutoffCurrent);
        float dt = 1.0f / (float)sampleRate;
        float alpha = dt / (rc + dt);

        float resonanceFactor = (resonanceCurrent - 0.1f) / 19.9f;
        alpha = alpha * (1.0f + resonanceFactor * 2.0f);
        alpha = min(alpha, 0.99f);

        float output = prevOutput + alpha * (input - prevOutput);
        prevOutput = clampSample(output);

        return output;
    }

    void setCutoff(float freq) { cutoff = clampF(freq, 20.0f, 20000.0f); }
    void setResonance(float res) { resonance = clampF(res, 0.1f, 20.0f); }
    float getCutoff() const { return cutoff; }

    void reset() {
        prevOutput = 0.0f;
        cutoffCurrent = cutoff;
        resonanceCurrent = resonance;
    }

private:
    int sampleRate;
    float cutoff;
    float cutoffCurrent;
    float resonance;
    float resonanceCurrent;
    float prevOutput;
    float smoothing;
};

// ============================================================================
// DC Blocker Class
// ============================================================================

class DCBlocker {
public:
    DCBlocker() : xPrev(0.0f), yPrev(0.0f), coeff(0.995f) {}

    float processSample(float input) {
        float output = input - xPrev + coeff * yPrev;
        xPrev = input;
        yPrev = output;
        return output;
    }

    void reset() { xPrev = 0.0f; yPrev = 0.0f; }

private:
    float xPrev;
    float yPrev;
    float coeff;
};

// ============================================================================
// Delay Effect Class (Optimized for ESP32 RAM constraints)
// ============================================================================

// Reduced to 0.35 seconds max delay to fit in ESP32 RAM
// At 44100 Hz: 0.35s = 15,435 samples × 4 bytes = ~60KB
#define MAX_DELAY_SAMPLES 15435

class DelayEffect {
public:
    DelayEffect(int sr = SAMPLE_RATE)
        : sampleRate(sr), writePos(0), delayTime(0.25f), feedback(0.3f), dryWet(0.3f),
          currentDelaySamples(sr * 0.25f), hpState(0.0f), lpState(0.0f),
          modPhase(0.0f), modDepth(0.003f), modRate(0.5f) {
        memset(buffer, 0, sizeof(buffer));
    }

    float processSample(float input) {
        float targetDelaySamples = delayTime * (float)sampleRate;

        // Smooth delay time changes
        float diff = targetDelaySamples - currentDelaySamples;
        float slewRate = 10.0f;
        if (fabs(diff) > slewRate) {
            currentDelaySamples += (diff > 0) ? slewRate : -slewRate;
        } else {
            currentDelaySamples = targetDelaySamples;
        }

        // Add subtle modulation
        float mod = sinf(TWO_PI_F * modRate * modPhase / (float)sampleRate);
        float modSamples = modDepth * (float)sampleRate * mod;
        modPhase += 1.0f;
        if (modPhase >= (float)sampleRate) modPhase = 0.0f;

        float totalDelaySamples = currentDelaySamples + modSamples;
        totalDelaySamples = clampF(totalDelaySamples, 1.0f, (float)(MAX_DELAY_SAMPLES - 2));

        // Read with linear interpolation
        float readPos = (float)writePos - totalDelaySamples;
        if (readPos < 0) readPos += (float)MAX_DELAY_SAMPLES;

        int readPosInt = (int)readPos;
        float frac = readPos - (float)readPosInt;
        int idx0 = readPosInt % MAX_DELAY_SAMPLES;
        int idx1 = (readPosInt + 1) % MAX_DELAY_SAMPLES;
        float delayed = buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;

        // Simple feedback filtering
        float hpCutoff = 80.0f / (float)sampleRate;
        float hpCoeff = 1.0f - expf(-TWO_PI_F * hpCutoff);
        hpState = clampSample(hpState + hpCoeff * (delayed - hpState));
        float filtered = delayed - hpState;

        float lpCutoff = 5000.0f / (float)sampleRate;
        float lpCoeff = 1.0f - expf(-TWO_PI_F * lpCutoff);
        lpState = clampSample(lpState + lpCoeff * (filtered - lpState));

        // Write to buffer
        buffer[writePos] = clampSample(input + lpState * feedback);
        writePos = (writePos + 1) % MAX_DELAY_SAMPLES;

        // Mix dry and wet
        return input * (1.0f - dryWet) + delayed * dryWet;
    }

    void setDelayTime(float timeSeconds) { delayTime = clampF(timeSeconds, 0.001f, 0.35f); }
    void setFeedback(float fb) { feedback = clampF(fb, 0.0f, 0.95f); }
    void setDryWet(float mix) { dryWet = clampF(mix, 0.0f, 1.0f); }

private:
    int sampleRate;
    float buffer[MAX_DELAY_SAMPLES];
    int writePos;
    float delayTime;
    float feedback;
    float dryWet;
    float currentDelaySamples;
    float hpState;
    float lpState;
    float modPhase;
    float modDepth;
    float modRate;
};

// ============================================================================
// Simple Reverb Effect (Optimized for ESP32)
// ============================================================================

// 2048 samples × 4 bytes = 8KB
#define REVERB_BUFFER_SIZE 2048

class SimpleReverb {
public:
    SimpleReverb()
        : writeIdx(0), size(0.7f), wet(0.3f), damping(0.5f) {
        memset(buffer, 0, sizeof(buffer));
        lpState = 0.0f;
    }

    float processSample(float input) {
        // Simple feedback delay reverb
        int delayLen = (int)(REVERB_BUFFER_SIZE * 0.7f);
        int readIdx = (writeIdx - delayLen + REVERB_BUFFER_SIZE) % REVERB_BUFFER_SIZE;

        float delayed = buffer[readIdx];

        // Damping filter
        float dampCoeff = 0.3f + damping * 0.5f;
        lpState = lpState + dampCoeff * (delayed - lpState);

        // Feedback
        float feedbackAmount = 0.3f + size * 0.5f;
        buffer[writeIdx] = clampSample(input + lpState * feedbackAmount);

        writeIdx = (writeIdx + 1) % REVERB_BUFFER_SIZE;

        // Mix
        float dry = 1.0f - wet;
        return input * dry + delayed * wet * 0.5f;
    }

    void setSize(float s) { size = clampF(s, 0.0f, 1.0f); }
    void setDryWet(float mix) { wet = clampF(mix, 0.0f, 1.0f); }
    void setDamping(float d) { damping = clampF(d, 0.0f, 1.0f); }

private:
    float buffer[REVERB_BUFFER_SIZE];
    int writeIdx;
    float size;
    float wet;
    float damping;
    float lpState;
};

// ============================================================================
// Audio Engine Class
// ============================================================================

class AudioEngine {
public:
    AudioEngine()
        : volume(0.7f), baseFrequency(440.0f), lfoPitchDepth(0.0f),
          pitchEnvMode(PitchEnvelopeMode::Up), currentFrequency(440.0f),
          frequencySmooth(440.0f, 0.08f), inReleasePhase(false), pitchEnvStartLevel(1.0f) {

        oscillator.setWaveform(Waveform::Square);
        lfo.setFrequency(2.0f);
        lfo.setDepth(0.5f);
        lfo.setWaveform(Waveform::Triangle);
        envelope.setAttack(0.01f);
        envelope.setRelease(0.5f);
        filter.setCutoff(3000.0f);
        delay.setDryWet(0.3f);
        delay.setFeedback(0.55f);
        reverb.setDryWet(0.3f);
    }

    float processSample() {
        // Generate envelope
        float envValue = envelope.generateSample();

        // Generate LFO
        float lfoValue = lfo.generateSample();

        // Calculate target frequency with pitch envelope
        float targetFreq = baseFrequency;

        if (inReleasePhase && pitchEnvMode != PitchEnvelopeMode::None) {
            float releaseProgress = 0.0f;
            if (pitchEnvStartLevel > 0.001f) {
                releaseProgress = 1.0f - (envValue / pitchEnvStartLevel);
                releaseProgress = clampF(releaseProgress, 0.0f, 1.0f);
            }

            if (pitchEnvMode == PitchEnvelopeMode::Up) {
                float pitchMult = powf(4.0f, releaseProgress);
                targetFreq = baseFrequency * pitchMult;
            } else if (pitchEnvMode == PitchEnvelopeMode::Down) {
                float pitchMult = powf(0.25f, releaseProgress);
                targetFreq = baseFrequency * pitchMult;
            }

            if (envValue < 0.001f) {
                inReleasePhase = false;
            }
        }

        // Apply LFO pitch modulation
        if (lfoPitchDepth > 0.001f) {
            float octaveShift = lfoValue * lfoPitchDepth;
            float pitchMult = powf(2.0f, octaveShift);
            targetFreq *= pitchMult;
        }

        // Smooth frequency
        frequencySmooth.setTarget(targetFreq);
        currentFrequency = frequencySmooth.getNext();
        oscillator.setFrequency(currentFrequency);

        // Generate oscillator
        float sample = oscillator.generateSample();

        // Apply LFO to filter
        float baseCutoff = filter.getCutoff();
        float modCutoff = baseCutoff * powf(2.0f, lfoValue * 3.0f);
        modCutoff = clampF(modCutoff, 20.0f, 12000.0f);
        filter.setCutoff(modCutoff);

        // Process filter
        sample = filter.processSample(sample);
        filter.setCutoff(baseCutoff);

        // Apply envelope
        if (envValue < 0.001f) {
            sample = 0.0f;
        } else {
            sample *= envValue;
        }

        // Apply delay
        sample = delay.processSample(sample);

        // Apply reverb
        sample = reverb.processSample(sample);

        // Apply DC blocking
        sample = dcBlocker.processSample(sample);

        // Apply volume and clamp
        sample = clampF(sample * volume, -1.0f, 1.0f);

        return sample;
    }

    void trigger() {
        oscillator.resetPhase();
        envelope.trigger();
        inReleasePhase = false;
    }

    void release() {
        pitchEnvStartLevel = envelope.getCurrentValue();
        inReleasePhase = true;
        envelope.release();
    }

    // Parameter setters
    void setVolume(float vol) { volume = clampF(vol, 0.0f, 1.0f); }
    void setFrequency(float freq) { baseFrequency = clampF(freq, 20.0f, 20000.0f); }
    void setWaveform(Waveform wf) { oscillator.setWaveform(wf); }
    void setWaveform(int index) { oscillator.setWaveform((Waveform)(index % 4)); }
    void setLfoRate(float rate) { lfo.setFrequency(rate); }
    void setLfoDepth(float depth) { lfo.setDepth(depth); }
    void setLfoPitchDepth(float depth) { lfoPitchDepth = clampF(depth, 0.0f, 1.0f); }
    void setFilterCutoff(float freq) { filter.setCutoff(freq); }
    void setFilterResonance(float res) { filter.setResonance(res); }
    void setDelayTime(float seconds) { delay.setDelayTime(seconds); }
    void setDelayFeedback(float fb) { delay.setFeedback(fb); }
    void setDelayMix(float mix) { delay.setDryWet(mix); }
    void setReverbSize(float size) { reverb.setSize(size); }
    void setReverbMix(float mix) { reverb.setDryWet(mix); }
    void setReleaseTime(float seconds) { envelope.setRelease(seconds); }
    void setPitchEnvelopeMode(PitchEnvelopeMode mode) { pitchEnvMode = mode; }

    const char* cyclePitchEnvelope() {
        switch (pitchEnvMode) {
            case PitchEnvelopeMode::None:
                pitchEnvMode = PitchEnvelopeMode::Up;
                return "UP";
            case PitchEnvelopeMode::Up:
                pitchEnvMode = PitchEnvelopeMode::Down;
                return "DOWN";
            case PitchEnvelopeMode::Down:
            default:
                pitchEnvMode = PitchEnvelopeMode::None;
                return "NONE";
        }
    }

    int cycleWaveform() {
        Waveform wf = oscillator.getWaveform();
        int next = ((int)wf + 1) % 4;
        oscillator.setWaveform((Waveform)next);
        return next;
    }

    bool isPlaying() const { return envelope.isActive(); }
    PitchEnvelopeMode getPitchEnvMode() const { return pitchEnvMode; }
    Waveform getWaveform() const { return oscillator.getWaveform(); }

private:
    Oscillator oscillator;
    LFO lfo;
    Envelope envelope;
    LowPassFilter filter;
    DCBlocker dcBlocker;
    DelayEffect delay;
    SimpleReverb reverb;

    float volume;
    float baseFrequency;
    float lfoPitchDepth;
    PitchEnvelopeMode pitchEnvMode;
    float currentFrequency;
    SmoothedValue frequencySmooth;
    bool inReleasePhase;
    float pitchEnvStartLevel;
};

// ============================================================================
// Global Objects
// ============================================================================

AudioEngine audioEngine;
volatile bool sirenActive = false;
volatile bool updateDisplay = true;
volatile int waveformIndex = 1;  // Square
volatile int pitchEnvIndex = 1;  // Up

// Button state
bool lastTriggerState = HIGH;
bool lastWaveformState = HIGH;
bool lastPitchEnvState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Audio buffer
int16_t audioBuffer[BUFFER_SIZE * 2];  // Stereo

// ============================================================================
// Display Mode & Debug Log System
// ============================================================================

enum class DisplayMode {
    Normal,       // Shows waveform, pitch env, status
    Oscilloscope, // Shows audio waveform visualization
    Debug         // Shows debug/error messages
};

volatile DisplayMode currentDisplayMode = DisplayMode::Normal;
unsigned long lastInteractionTime = 0;
const unsigned long OSCILLOSCOPE_TIMEOUT = 1000;  // 1 second

// Debug log system - circular buffer of messages
#define MAX_LOG_LINES 5
#define MAX_LOG_LENGTH 22  // Fits on 128px wide OLED with small font
char debugLog[MAX_LOG_LINES][MAX_LOG_LENGTH];
int debugLogHead = 0;
int debugLogCount = 0;
unsigned long debugLogExpiry = 0;
const unsigned long DEBUG_DISPLAY_TIME = 3000;  // Show debug for 3 seconds

// Oscilloscope buffer - stores samples for visualization
#define SCOPE_WIDTH 128   // OLED width
volatile int8_t scopeBuffer[SCOPE_WIDTH];
volatile int scopeWriteIdx = 0;
volatile bool scopeBufferReady = false;

// Add a debug message to the log
void debugPrint(const char* msg) {
    // Copy to circular buffer
    strncpy(debugLog[debugLogHead], msg, MAX_LOG_LENGTH - 1);
    debugLog[debugLogHead][MAX_LOG_LENGTH - 1] = '\0';

    debugLogHead = (debugLogHead + 1) % MAX_LOG_LINES;
    if (debugLogCount < MAX_LOG_LINES) debugLogCount++;

    // Set expiry time and switch to debug mode
    debugLogExpiry = millis() + DEBUG_DISPLAY_TIME;
    currentDisplayMode = DisplayMode::Debug;
    updateDisplay = true;

    // Also print to serial
    Serial.print("[DEBUG] ");
    Serial.println(msg);
}

// Log with formatting (limited to simple cases)
void debugPrintf(const char* fmt, int value) {
    char buf[MAX_LOG_LENGTH];
    snprintf(buf, MAX_LOG_LENGTH, fmt, value);
    debugPrint(buf);
}

void debugPrintf(const char* fmt, const char* str) {
    char buf[MAX_LOG_LENGTH];
    snprintf(buf, MAX_LOG_LENGTH, fmt, str);
    debugPrint(buf);
}

// Record user interaction (resets oscilloscope timer)
void recordInteraction() {
    lastInteractionTime = millis();
    if (currentDisplayMode == DisplayMode::Oscilloscope) {
        currentDisplayMode = DisplayMode::Normal;
        updateDisplay = true;
    }
}

// Check if we should switch to oscilloscope mode
void checkDisplayMode() {
    unsigned long now = millis();

    // If in debug mode, check if it should expire
    if (currentDisplayMode == DisplayMode::Debug) {
        if (now > debugLogExpiry) {
            currentDisplayMode = DisplayMode::Normal;
            lastInteractionTime = now;  // Reset interaction timer
            updateDisplay = true;
        }
        return;
    }

    // If no interaction for OSCILLOSCOPE_TIMEOUT, switch to oscilloscope
    if (currentDisplayMode == DisplayMode::Normal) {
        if ((now - lastInteractionTime) > OSCILLOSCOPE_TIMEOUT) {
            currentDisplayMode = DisplayMode::Oscilloscope;
            updateDisplay = true;
        }
    }
}

// ============================================================================
// I2S Setup
// ============================================================================

void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_DATA_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM);
}

// ============================================================================
// Display Functions
// ============================================================================

const char* getWaveformName(int index) {
    switch (index) {
        case 0: return "SINE";
        case 1: return "SQUARE";
        case 2: return "SAW";
        case 3: return "TRIANGLE";
        default: return "???";
    }
}

const char* getPitchEnvName(int index) {
    switch (index) {
        case 0: return "NONE";
        case 1: return "UP";
        case 2: return "DOWN";
        default: return "???";
    }
}

void drawNormalDisplay() {
    // Title (large font)
    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.drawStr(0, 14, "DUB SIREN");

    // Status (smaller font)
    u8g2.setFont(u8g2_font_helvR08_tr);

    // Waveform
    char wfStr[24];
    snprintf(wfStr, sizeof(wfStr), "Wave: %s", getWaveformName(waveformIndex));
    u8g2.drawStr(0, 28, wfStr);

    // Pitch Envelope
    char peStr[24];
    snprintf(peStr, sizeof(peStr), "Pitch: %s", getPitchEnvName(pitchEnvIndex));
    u8g2.drawStr(0, 40, peStr);

    // Active state
    if (sirenActive) {
        u8g2.drawStr(0, 54, ">>> ACTIVE <<<");
    } else {
        u8g2.drawStr(0, 54, "Press PRG to trigger");
    }
}

void drawOscilloscope() {
    // Draw title bar
    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.drawStr(0, 10, getWaveformName(waveformIndex));

    // Draw active indicator
    if (sirenActive) {
        u8g2.drawStr(80, 10, "[PLAY]");
    }

    // Draw center line (zero crossing) - dotted
    int centerY = 40;
    for (int x = 0; x < 128; x += 4) {
        u8g2.drawPixel(x, centerY);
    }

    // Draw waveform from scope buffer
    int prevY = centerY;
    for (int x = 0; x < SCOPE_WIDTH; x++) {
        // Read from buffer with offset to get continuous waveform
        int idx = (scopeWriteIdx + x) % SCOPE_WIDTH;
        int8_t sample = scopeBuffer[idx];

        // Scale sample (-128 to 127) to display area
        int y = centerY - (sample * 20 / 128);  // Scale to +/- 20 pixels
        y = constrain(y, 14, 62);

        // Draw line from previous point for smooth waveform
        if (x > 0) {
            u8g2.drawLine(x - 1, prevY, x, y);
        }
        prevY = y;
    }

    // Draw border
    u8g2.drawFrame(0, 12, 128, 52);
}

void drawDebugLog() {
    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.drawStr(0, 10, "=== DEBUG LOG ===");

    // Draw log messages
    int y = 22;
    for (int i = 0; i < debugLogCount && i < MAX_LOG_LINES; i++) {
        // Calculate index to read from (oldest first)
        int idx;
        if (debugLogCount < MAX_LOG_LINES) {
            idx = i;
        } else {
            idx = (debugLogHead + i) % MAX_LOG_LINES;
        }
        u8g2.drawStr(0, y, debugLog[idx]);
        y += 10;
    }

    // Show remaining time
    unsigned long remaining = 0;
    if (debugLogExpiry > millis()) {
        remaining = (debugLogExpiry - millis()) / 1000 + 1;
    }
    char timeStr[24];
    snprintf(timeStr, sizeof(timeStr), "Auto-close: %lus", remaining);
    u8g2.drawStr(0, 62, timeStr);
}

void updateOLED() {
    u8g2.clearBuffer();

    switch (currentDisplayMode) {
        case DisplayMode::Normal:
            drawNormalDisplay();
            break;
        case DisplayMode::Oscilloscope:
            drawOscilloscope();
            break;
        case DisplayMode::Debug:
            drawDebugLog();
            break;
    }

    u8g2.sendBuffer();
}

// ============================================================================
// Button Handling
// ============================================================================

void handleButtons() {
    unsigned long currentTime = millis();

    // Trigger button (PRG)
    bool triggerState = digitalRead(TRIGGER_BTN_PIN);
    if (triggerState != lastTriggerState && (currentTime - lastDebounceTime) > debounceDelay) {
        lastDebounceTime = currentTime;
        recordInteraction();  // Reset oscilloscope timer
        if (triggerState == LOW) {
            // Button pressed
            audioEngine.trigger();
            sirenActive = true;
            updateDisplay = true;
        } else {
            // Button released
            audioEngine.release();
            sirenActive = false;
            updateDisplay = true;
        }
    }
    lastTriggerState = triggerState;

    // Waveform button (optional)
    bool waveformState = digitalRead(WAVEFORM_BTN_PIN);
    if (waveformState != lastWaveformState && (currentTime - lastDebounceTime) > debounceDelay) {
        lastDebounceTime = currentTime;
        recordInteraction();  // Reset oscilloscope timer
        if (waveformState == LOW) {
            waveformIndex = audioEngine.cycleWaveform();
            updateDisplay = true;
        }
    }
    lastWaveformState = waveformState;

    // Pitch envelope button (optional)
    bool pitchEnvState = digitalRead(PITCHENV_BTN_PIN);
    if (pitchEnvState != lastPitchEnvState && (currentTime - lastDebounceTime) > debounceDelay) {
        lastDebounceTime = currentTime;
        recordInteraction();  // Reset oscilloscope timer
        if (pitchEnvState == LOW) {
            audioEngine.cyclePitchEnvelope();
            pitchEnvIndex = (pitchEnvIndex + 1) % 3;
            updateDisplay = true;
        }
    }
    lastPitchEnvState = pitchEnvState;
}

// ============================================================================
// Audio Task (runs on Core 1)
// ============================================================================

void audioTask(void *pvParameters) {
    size_t bytesWritten;
    int scopeSampleCounter = 0;
    const int SCOPE_DOWNSAMPLE = SAMPLE_RATE / SCOPE_WIDTH / 30;  // ~30 fps update

    while (true) {
        // Generate audio buffer
        for (int i = 0; i < BUFFER_SIZE; i++) {
            float sample = audioEngine.processSample();
            int16_t sampleInt = (int16_t)(sample * 32767.0f);
            audioBuffer[i * 2] = sampleInt;      // Left
            audioBuffer[i * 2 + 1] = sampleInt;  // Right

            // Capture samples for oscilloscope (downsampled)
            scopeSampleCounter++;
            if (scopeSampleCounter >= SCOPE_DOWNSAMPLE) {
                scopeSampleCounter = 0;
                // Convert to 8-bit for display (-128 to 127)
                scopeBuffer[scopeWriteIdx] = (int8_t)(sample * 127.0f);
                scopeWriteIdx = (scopeWriteIdx + 1) % SCOPE_WIDTH;
            }
        }

        // Write to I2S
        i2s_write(I2S_NUM, audioBuffer, sizeof(audioBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Start serial FIRST for debugging
    Serial.begin(115200);
    delay(2000);  // Wait for serial to stabilize and user to open monitor
    Serial.println("\n\n=============================");
    Serial.println("=== Dub Siren ESP32 ===");
    Serial.println("Heltec WiFi LoRa 32 V4");
    Serial.println("=============================");
    Serial.println("Starting initialization...");
    Serial.println("");

    // Enable Vext to power the OLED (Heltec specific)
    // On Heltec V4, Vext must be LOW to enable power to OLED
    Serial.println("[1/5] Enabling Vext power...");
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);  // LOW = power ON for Heltec V4
    delay(200);  // Wait for power to stabilize
    Serial.println("      Vext enabled (GPIO 36 = LOW)");

    // Reset OLED manually with proper timing
    Serial.println("[2/5] Resetting OLED display...");
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, HIGH);
    delay(10);
    digitalWrite(OLED_RST, LOW);
    delay(100);  // Hold reset low
    digitalWrite(OLED_RST, HIGH);
    delay(100);  // Wait after reset
    Serial.println("      OLED reset complete");

    // Initialize OLED display
    Serial.println("[3/5] Initializing U8g2 display...");
    Serial.print("      SDA=GPIO"); Serial.print(OLED_SDA);
    Serial.print(" SCL=GPIO"); Serial.println(OLED_SCL);

    u8g2.begin();
    Serial.println("      u8g2.begin() complete");

    u8g2.setContrast(255);  // Max brightness
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.drawStr(10, 30, "DUB SIREN");
    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.drawStr(10, 45, "Heltec V4");
    u8g2.drawStr(10, 58, "Initializing...");
    u8g2.sendBuffer();
    Serial.println("      Display initialized and showing splash!");

    // Configure buttons
    Serial.println("[4/5] Configuring buttons...");
    pinMode(TRIGGER_BTN_PIN, INPUT_PULLUP);
    pinMode(WAVEFORM_BTN_PIN, INPUT_PULLUP);
    pinMode(PITCHENV_BTN_PIN, INPUT_PULLUP);
    Serial.println("      PRG=GPIO0, BTN1=GPIO47, BTN2=GPIO48");

    // Initialize I2S
    Serial.println("[5/5] Initializing I2S audio...");
    Serial.print("      BCK=GPIO"); Serial.print(I2S_BCK_PIN);
    Serial.print(" WS=GPIO"); Serial.print(I2S_WS_PIN);
    Serial.print(" DATA=GPIO"); Serial.println(I2S_DATA_PIN);
    setupI2S();
    Serial.println("      I2S initialized!");

    // Initialize interaction timer
    lastInteractionTime = millis();

    // Initialize scope buffer
    memset((void*)scopeBuffer, 0, sizeof(scopeBuffer));

    // Show initial display
    Serial.println("");
    Serial.println("===== SETUP COMPLETE =====");
    updateOLED();

    // Create audio task on Core 1
    xTaskCreatePinnedToCore(
        audioTask,      // Function
        "AudioTask",    // Name
        4096,           // Stack size
        NULL,           // Parameters
        configMAX_PRIORITIES - 1,  // Priority (highest)
        NULL,           // Task handle
        1               // Core 1
    );

    Serial.println("Audio task started on Core 1");
    Serial.println("\nControls:");
    Serial.println("  PRG Button: Trigger/Release siren");
    Serial.println("  GPIO 47: Cycle waveform");
    Serial.println("  GPIO 48: Cycle pitch envelope");
    Serial.println("\nSerial commands:");
    Serial.println("  t - Trigger");
    Serial.println("  r - Release");
    Serial.println("  w - Cycle waveform");
    Serial.println("  p - Cycle pitch envelope");
    Serial.println("  d - Test debug display");
    Serial.println("  1-9 - Set frequency (220-880 Hz)");
    Serial.println("\nDisplay modes:");
    Serial.println("  Normal    - Default status view");
    Serial.println("  Scope     - Auto after 1s idle");
    Serial.println("  Debug     - Shows on errors (3s)");
}

// ============================================================================
// Main Loop (runs on Core 0)
// ============================================================================

// Oscilloscope refresh timing
unsigned long lastScopeUpdate = 0;
const unsigned long SCOPE_UPDATE_INTERVAL = 33;  // ~30 fps

void loop() {
    // Handle physical buttons
    handleButtons();

    // Handle serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        recordInteraction();  // Any serial input counts as interaction
        switch (cmd) {
            case 't':
            case 'T':
                audioEngine.trigger();
                sirenActive = true;
                updateDisplay = true;
                debugPrint("Triggered!");
                break;
            case 'r':
            case 'R':
                audioEngine.release();
                sirenActive = false;
                updateDisplay = true;
                debugPrint("Released!");
                break;
            case 'w':
            case 'W':
                waveformIndex = audioEngine.cycleWaveform();
                updateDisplay = true;
                debugPrintf("Wave: %s", getWaveformName(waveformIndex));
                break;
            case 'p':
            case 'P':
                audioEngine.cyclePitchEnvelope();
                pitchEnvIndex = (pitchEnvIndex + 1) % 3;
                updateDisplay = true;
                debugPrintf("Pitch: %s", getPitchEnvName(pitchEnvIndex));
                break;
            case '1': audioEngine.setFrequency(220.0f); debugPrint("Freq: 220Hz"); break;
            case '2': audioEngine.setFrequency(261.63f); debugPrint("Freq: 261Hz C4"); break;
            case '3': audioEngine.setFrequency(329.63f); debugPrint("Freq: 330Hz E4"); break;
            case '4': audioEngine.setFrequency(392.0f); debugPrint("Freq: 392Hz G4"); break;
            case '5': audioEngine.setFrequency(440.0f); debugPrint("Freq: 440Hz A4"); break;
            case '6': audioEngine.setFrequency(523.25f); debugPrint("Freq: 523Hz C5"); break;
            case '7': audioEngine.setFrequency(659.25f); debugPrint("Freq: 659Hz E5"); break;
            case '8': audioEngine.setFrequency(783.99f); debugPrint("Freq: 784Hz G5"); break;
            case '9': audioEngine.setFrequency(880.0f); debugPrint("Freq: 880Hz A5"); break;
            case 'd':
            case 'D':
                // Manual debug test
                debugPrint("Debug test message");
                break;
        }
    }

    // Check if display mode should change
    checkDisplayMode();

    // Update display based on mode
    if (currentDisplayMode == DisplayMode::Oscilloscope) {
        // Continuous update for oscilloscope at ~30fps
        unsigned long now = millis();
        if ((now - lastScopeUpdate) >= SCOPE_UPDATE_INTERVAL) {
            lastScopeUpdate = now;
            updateOLED();
        }
    } else if (updateDisplay) {
        updateOLED();
        updateDisplay = false;
    }

    // Small delay to prevent watchdog issues
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
