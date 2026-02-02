/*
 * DubSiren ESP32 DSP Library - XIAO Edition
 * Optimized for limited RAM on XIAO ESP32S3
 *
 * Reduced delay buffer, no reverb
 */

#ifndef DSP_H
#define DSP_H

#include <Arduino.h>
#include <cmath>

// ============================================================================
// CONFIGURATION
// ============================================================================

constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 128;  // Smaller buffer for XIAO
constexpr float PI_F = 3.14159265358979323846f;
constexpr float TWO_PI_F = 2.0f * PI_F;

// ============================================================================
// WAVEFORM TYPES
// ============================================================================

enum class Waveform : uint8_t {
    Sine = 0,
    Square = 1,
    Saw = 2,
    Triangle = 3
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

inline float clampF(float value, float minVal, float maxVal) {
    return max(minVal, min(maxVal, value));
}

inline float lerpF(float a, float b, float t) {
    return a + t * (b - a);
}

inline float fastTanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ============================================================================
// OSCILLATOR - PolyBLEP anti-aliased waveforms
// ============================================================================

class Oscillator {
public:
    Oscillator() : frequency(440.0f), phase(0.0f), waveform(Waveform::Sine) {
        phaseInc = frequency / SAMPLE_RATE;
    }

    void setFrequency(float freq) {
        frequency = clampF(freq, 20.0f, 20000.0f);
        phaseInc = frequency / SAMPLE_RATE;
    }

    void setWaveform(Waveform wf) {
        waveform = wf;
    }

    void resetPhase() {
        phase = 0.0f;
    }

    float generate() {
        float sample = 0.0f;

        switch (waveform) {
            case Waveform::Sine:
                sample = sinf(phase * TWO_PI_F);
                break;

            case Waveform::Square:
                sample = phase < 0.5f ? 1.0f : -1.0f;
                sample += polyBlep(phase, phaseInc);
                sample -= polyBlep(fmodf(phase + 0.5f, 1.0f), phaseInc);
                break;

            case Waveform::Saw:
                sample = 2.0f * phase - 1.0f;
                sample -= polyBlep(phase, phaseInc);
                break;

            case Waveform::Triangle:
                sample = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
                break;
        }

        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;

        return sample;
    }

    float getFrequency() const { return frequency; }
    Waveform getWaveform() const { return waveform; }

private:
    float frequency;
    float phase;
    float phaseInc;
    Waveform waveform;

    float polyBlep(float t, float dt) const {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        } else if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }
};

// ============================================================================
// LFO - Low Frequency Oscillator
// ============================================================================

class LFO {
public:
    LFO() : frequency(1.0f), phase(0.0f), depth(1.0f), waveform(Waveform::Sine) {
        phaseInc = frequency / SAMPLE_RATE;
    }

    void setFrequency(float freq) {
        frequency = clampF(freq, 0.01f, 50.0f);
        phaseInc = frequency / SAMPLE_RATE;
    }

    void setDepth(float d) {
        depth = clampF(d, 0.0f, 1.0f);
    }

    void setWaveform(Waveform wf) {
        waveform = wf;
    }

    float generate() {
        float sample = 0.0f;

        switch (waveform) {
            case Waveform::Sine:
                sample = sinf(phase * TWO_PI_F);
                break;
            case Waveform::Square:
                sample = phase < 0.5f ? 1.0f : -1.0f;
                break;
            case Waveform::Saw:
                sample = 2.0f * phase - 1.0f;
                break;
            case Waveform::Triangle:
                sample = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
                break;
        }

        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;

        return sample * depth;
    }

private:
    float frequency;
    float phase;
    float phaseInc;
    float depth;
    Waveform waveform;
};

// ============================================================================
// ENVELOPE - Simple attack/release
// ============================================================================

class Envelope {
public:
    Envelope() : attackTime(0.01f), releaseTime(0.5f), currentValue(0.0f), active(false) {
        updateCoefficients();
    }

    void setAttack(float seconds) {
        attackTime = max(0.001f, seconds);
        updateCoefficients();
    }

    void setRelease(float seconds) {
        releaseTime = max(0.001f, seconds);
        updateCoefficients();
    }

    void trigger() {
        active = true;
    }

    void release() {
        active = false;
    }

    float generate() {
        float target = active ? 1.0f : 0.0f;
        float coeff = active ? attackCoeff : releaseCoeff;
        currentValue += (target - currentValue) * coeff;
        return currentValue;
    }

    bool isActive() const { return active || currentValue > 0.001f; }
    float getValue() const { return currentValue; }

private:
    float attackTime;
    float releaseTime;
    float attackCoeff;
    float releaseCoeff;
    float currentValue;
    bool active;

    void updateCoefficients() {
        attackCoeff = 1.0f - expf(-1.0f / (attackTime * SAMPLE_RATE));
        releaseCoeff = 1.0f - expf(-1.0f / (releaseTime * SAMPLE_RATE));
    }
};

// ============================================================================
// FILTER - One-pole low-pass with resonance
// ============================================================================

class LowPassFilter {
public:
    LowPassFilter() : cutoff(5000.0f), resonance(0.0f), prevOutput(0.0f) {
        updateCoefficient();
    }

    void setCutoff(float freq) {
        cutoff = clampF(freq, 20.0f, SAMPLE_RATE * 0.45f);
        updateCoefficient();
    }

    void setResonance(float res) {
        resonance = clampF(res, 0.0f, 0.95f);
    }

    float process(float input) {
        float fb = prevOutput * resonance * 4.0f;
        float in = input - fb;
        prevOutput += coeff * (in - prevOutput);
        prevOutput = fastTanh(prevOutput);
        return prevOutput;
    }

    void reset() {
        prevOutput = 0.0f;
    }

private:
    float cutoff;
    float resonance;
    float coeff;
    float prevOutput;

    void updateCoefficient() {
        float omega = TWO_PI_F * cutoff / SAMPLE_RATE;
        coeff = omega / (omega + 1.0f);
    }
};

// ============================================================================
// DELAY - Compact version for XIAO (0.5 second max)
// ============================================================================

class DelayEffect {
public:
    static constexpr int MAX_DELAY_SAMPLES = 22050;  // 0.5 seconds

    DelayEffect() : delayTime(0.25f), feedback(0.4f), dryWet(0.3f),
                   writePos(0), currentDelay(0.0f), lpState(0.0f) {
        memset(buffer, 0, sizeof(buffer));
        targetDelay = delayTime * SAMPLE_RATE;
        currentDelay = targetDelay;
    }

    void setDelayTime(float seconds) {
        delayTime = clampF(seconds, 0.01f, 0.5f);
        targetDelay = delayTime * SAMPLE_RATE;
    }

    void setFeedback(float fb) {
        feedback = clampF(fb, 0.0f, 0.9f);
    }

    void setDryWet(float mix) {
        dryWet = clampF(mix, 0.0f, 1.0f);
    }

    float process(float input) {
        currentDelay += (targetDelay - currentDelay) * 0.001f;

        float readPos = writePos - currentDelay;
        if (readPos < 0) readPos += MAX_DELAY_SAMPLES;

        int readIdx = (int)readPos;
        float frac = readPos - readIdx;
        int nextIdx = (readIdx + 1) % MAX_DELAY_SAMPLES;

        float delayed = lerpF(buffer[readIdx], buffer[nextIdx], frac);

        lpState += 0.3f * (delayed - lpState);

        buffer[writePos] = input + lpState * feedback;
        writePos = (writePos + 1) % MAX_DELAY_SAMPLES;

        return input * (1.0f - dryWet) + delayed * dryWet;
    }

private:
    float buffer[MAX_DELAY_SAMPLES];
    float delayTime;
    float feedback;
    float dryWet;
    int writePos;
    float targetDelay;
    float currentDelay;
    float lpState;
};

// ============================================================================
// DC BLOCKER
// ============================================================================

class DCBlocker {
public:
    DCBlocker() : xPrev(0.0f), yPrev(0.0f) {}

    float process(float input) {
        float output = input - xPrev + 0.995f * yPrev;
        xPrev = input;
        yPrev = output;
        return output;
    }

    void reset() {
        xPrev = 0.0f;
        yPrev = 0.0f;
    }

private:
    float xPrev;
    float yPrev;
};

#endif // DSP_H
