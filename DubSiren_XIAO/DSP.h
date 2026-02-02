/*
 * DubSiren ESP32 DSP Library - XIAO Edition
 * For XIAO ESP32S3 with 8MB PSRAM
 *
 * Full DSP chain: Oscillator, LFO, Envelope, Filter, Delay, Reverb
 */

#ifndef DSP_H
#define DSP_H

#include <Arduino.h>
#include <cmath>

// ============================================================================
// CONFIGURATION
// ============================================================================

constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 128;
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
// DELAY - Full version with PSRAM (1 second max)
// ============================================================================

class DelayEffect {
public:
    static constexpr int MAX_DELAY_SAMPLES = 44100;  // 1 second with PSRAM

    DelayEffect() : delayTime(0.25f), feedback(0.4f), dryWet(0.3f),
                   writePos(0), currentDelay(0.0f), lpState(0.0f) {
        memset(buffer, 0, sizeof(buffer));
        targetDelay = delayTime * SAMPLE_RATE;
        currentDelay = targetDelay;
    }

    void setDelayTime(float seconds) {
        delayTime = clampF(seconds, 0.01f, 1.0f);
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

// ============================================================================
// REVERB - Dub-style spring/room reverb
// ============================================================================

class ReverbEffect {
public:
    // Comb filter delay lengths (in samples) - tuned for dub character
    static constexpr int COMB1_LEN = 1557;
    static constexpr int COMB2_LEN = 1617;
    static constexpr int COMB3_LEN = 1491;
    static constexpr int COMB4_LEN = 1422;

    // Allpass filter delay lengths
    static constexpr int AP1_LEN = 225;
    static constexpr int AP2_LEN = 556;
    static constexpr int AP3_LEN = 441;

    ReverbEffect() : roomSize(0.7f), damping(0.4f), dryWet(0.3f) {
        // Initialize all buffers to zero
        memset(comb1, 0, sizeof(comb1));
        memset(comb2, 0, sizeof(comb2));
        memset(comb3, 0, sizeof(comb3));
        memset(comb4, 0, sizeof(comb4));
        memset(ap1, 0, sizeof(ap1));
        memset(ap2, 0, sizeof(ap2));
        memset(ap3, 0, sizeof(ap3));

        comb1Idx = comb2Idx = comb3Idx = comb4Idx = 0;
        ap1Idx = ap2Idx = ap3Idx = 0;
        lp1 = lp2 = lp3 = lp4 = 0.0f;

        updateCoefficients();
    }

    void setRoomSize(float size) {
        roomSize = clampF(size, 0.0f, 1.0f);
        updateCoefficients();
    }

    void setDamping(float damp) {
        damping = clampF(damp, 0.0f, 1.0f);
        updateCoefficients();
    }

    void setDryWet(float mix) {
        dryWet = clampF(mix, 0.0f, 1.0f);
    }

    float process(float input) {
        float wet = 0.0f;

        // Parallel comb filters with damping
        wet += processComb(input, comb1, comb1Idx, COMB1_LEN, lp1);
        wet += processComb(input, comb2, comb2Idx, COMB2_LEN, lp2);
        wet += processComb(input, comb3, comb3Idx, COMB3_LEN, lp3);
        wet += processComb(input, comb4, comb4Idx, COMB4_LEN, lp4);

        wet *= 0.25f;  // Average the comb outputs

        // Series allpass filters for diffusion
        wet = processAllpass(wet, ap1, ap1Idx, AP1_LEN);
        wet = processAllpass(wet, ap2, ap2Idx, AP2_LEN);
        wet = processAllpass(wet, ap3, ap3Idx, AP3_LEN);

        // Mix dry and wet
        return input * (1.0f - dryWet) + wet * dryWet;
    }

    float getRoomSize() const { return roomSize; }
    float getDryWet() const { return dryWet; }

private:
    // Comb filter buffers
    float comb1[COMB1_LEN];
    float comb2[COMB2_LEN];
    float comb3[COMB3_LEN];
    float comb4[COMB4_LEN];
    int comb1Idx, comb2Idx, comb3Idx, comb4Idx;
    float lp1, lp2, lp3, lp4;  // Lowpass states for damping

    // Allpass filter buffers
    float ap1[AP1_LEN];
    float ap2[AP2_LEN];
    float ap3[AP3_LEN];
    int ap1Idx, ap2Idx, ap3Idx;

    // Parameters
    float roomSize;
    float damping;
    float dryWet;
    float feedback;
    float dampCoeff;

    void updateCoefficients() {
        feedback = 0.7f + roomSize * 0.28f;  // 0.7 to 0.98
        dampCoeff = damping * 0.4f;  // Damping amount
    }

    float processComb(float input, float* buffer, int& idx, int len, float& lpState) {
        float output = buffer[idx];

        // Lowpass filter in feedback path (damping)
        lpState = output * (1.0f - dampCoeff) + lpState * dampCoeff;

        // Write new sample with feedback
        buffer[idx] = input + lpState * feedback;

        // Advance index
        idx = (idx + 1) % len;

        return output;
    }

    float processAllpass(float input, float* buffer, int& idx, int len) {
        float delayed = buffer[idx];
        float output = delayed - input;

        buffer[idx] = input + delayed * 0.5f;
        idx = (idx + 1) % len;

        return output;
    }
};

#endif // DSP_H
