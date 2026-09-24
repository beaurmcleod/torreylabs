#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tether
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain (float db) noexcept           { return std::pow (10.0f, db * 0.05f); }
inline float energyToDb (float e) noexcept          { return 10.0f * std::log10 (std::max (e, 1.0e-12f)); }
inline float midiToHz (float note) noexcept         { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
inline float hzToMidi (float hz) noexcept           { return 69.0f + 12.0f * std::log2 (std::max (hz, 1.0e-3f) / 440.0f); }

/** Wraps a phase into [-pi, pi). */
inline float princarg (float phase) noexcept
{
    return phase - kTwoPi * std::floor ((phase + kPi) / kTwoPi);
}

/** Coefficient for y += a * (x - y), updated `updatesPerSecond` times per second. */
inline float onePoleCoeff (float timeSeconds, double updatesPerSecond) noexcept
{
    if (timeSeconds <= 0.0f || updatesPerSecond <= 0.0)
        return 1.0f;

    return 1.0f - (float) std::exp (-1.0 / ((double) timeSeconds * updatesPerSecond));
}

inline int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

inline int log2Int (int powerOfTwo) noexcept
{
    int order = 0;
    while ((1 << order) < powerOfTwo)
        ++order;
    return order;
}

//==============================================================================
/** Attack/release one-pole smoother, usually fed with signal energy (x^2). */
struct EnvelopeFollower
{
    void setTimes (float attackMs, float releaseMs, double sampleRate) noexcept
    {
        attackCoeff  = onePoleCoeff (attackMs * 0.001f, sampleRate);
        releaseCoeff = onePoleCoeff (releaseMs * 0.001f, sampleRate);
    }

    float process (float x) noexcept
    {
        state += (x > state ? attackCoeff : releaseCoeff) * (x - state);
        return state;
    }

    void reset (float value = 0.0f) noexcept   { state = value; }

    float state = 0.0f, attackCoeff = 1.0f, releaseCoeff = 1.0f;
};

//==============================================================================
/** Moving average (boxcar). Used on x^2 it gives a true RMS that reaches zero
    exactly `length` samples after a signal stops, unlike a one-pole smoother. */
class MovingAverage
{
public:
    void prepare (int maxLength)
    {
        buffer.assign ((size_t) std::max (1, maxLength), 0.0f);
        setLength (maxLength);
    }

    void setLength (int newLength) noexcept
    {
        length = std::clamp (newLength, 1, (int) buffer.size());
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
        sum = 0.0;
    }

    float process (float x) noexcept
    {
        sum += (double) x - (double) buffer[(size_t) pos];
        buffer[(size_t) pos] = x;

        if (++pos == length)
        {
            // Re-sum once per cycle so floating-point drift can't accumulate.
            pos = 0;
            double exact = 0.0;
            for (int i = 0; i < length; ++i)
                exact += buffer[(size_t) i];
            sum = exact;
        }

        return (float) std::max (0.0, sum / length);
    }

    int getLength() const noexcept   { return length; }

private:
    std::vector<float> buffer;
    int length = 1, pos = 0;
    double sum = 0.0;
};

//==============================================================================
/** Power-of-two circular buffer holding the most recent samples of a signal. */
class SampleRing
{
public:
    void prepare (int minimumCapacity)
    {
        data.assign ((size_t) nextPowerOfTwo (std::max (minimumCapacity, 2)), 0.0f);
        mask = (int) data.size() - 1;
        writePos = 0;
    }

    void clear() noexcept
    {
        std::fill (data.begin(), data.end(), 0.0f);
        writePos = 0;
    }

    void push (float x) noexcept
    {
        data[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** The sample pushed `delay` pushes ago (0 = newest). */
    float delayed (int delay) const noexcept
    {
        return data[(size_t) ((writePos - 1 - delay) & mask)];
    }

    /** Copies the newest `n` samples, oldest first. */
    void copyLatest (float* dest, int n) const noexcept
    {
        int start = (writePos - n) & mask;
        for (int i = 0; i < n; ++i)
            dest[i] = data[(size_t) ((start + i) & mask)];
    }

    int capacity() const noexcept   { return (int) data.size(); }

private:
    std::vector<float> data;
    int mask = 0, writePos = 0;
};

//==============================================================================
/** Lock-free single-producer / single-consumer queue for small POD structs. */
template <typename T, int Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);

        if (w - r >= (uint32_t) Capacity)
            return false;

        items[(size_t) (w & (uint32_t) (Capacity - 1))] = item;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);

        if (r == w)
            return false;

        item = items[(size_t) (r & (uint32_t) (Capacity - 1))];
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

private:
    T items[(size_t) Capacity] {};
    std::atomic<uint32_t> writeIndex { 0 }, readIndex { 0 };
};

} // namespace tether
