#include "GrainShifter.h"

namespace tether
{

int GrainShifter::minimumDelay (int maxPeriod, int hopSize) noexcept
{
    // Marks are complete to about 1.125 periods behind the newest input, and a
    // grain may reach 1.5 periods past the end of the block being rendered.
    return hopSize + (int) std::ceil (1.625 * maxPeriod) + 8;
}

void GrainShifter::prepare (int newMaxPeriod, int hopSize, int newDelay)
{
    const int longestPeriod = std::max (16, newMaxPeriod);
    const int longestHop = std::max (1, hopSize);
    const int longestDelay = std::max (newDelay, minimumDelay (longestPeriod, longestHop));

    // Reads reach back at most 3.5 periods before the start of the block.
    const int capacity = nextPowerOfTwo (longestDelay + 4 * longestPeriod + longestHop + 16);
    for (auto& r : ring)
        r.assign ((size_t) capacity, 0.0f);
    mono.assign ((size_t) capacity, 0.0f);
    ringMask = capacity - 1;

    const int markCapacity = nextPowerOfTwo ((longestDelay + 4 * longestPeriod) / minPeriod + 16);
    marks.assign ((size_t) markCapacity, {});
    markMask = markCapacity - 1;

    // Output periods are never shorter than 4 samples.
    const int grainCapacity = nextPowerOfTwo ((longestHop + 4 * longestPeriod) / 4 + 16);
    grains.assign ((size_t) grainCapacity, {});
    grainMask = grainCapacity - 1;

    reference.assign ((size_t) longestPeriod + 8, 0.0f);
    candidates.assign ((size_t) (longestPeriod + longestPeriod / 4) + 16, 0.0f);
    referenceCoarse.assign (reference.size(), 0.0f);
    candidatesCoarse.assign (candidates.size(), 0.0f);
    scores.assign ((size_t) longestPeriod / 4 + 16, 0.0f);

    configure (longestPeriod, longestHop, longestDelay);
}

void GrainShifter::configure (int newMaxPeriod, int hopSize, int newDelay) noexcept
{
    maxPeriod = std::clamp (newMaxPeriod, 16, (int) reference.size() - 8);
    hop = std::max (1, hopSize);
    delay = std::clamp (newDelay, minimumDelay (maxPeriod, hop), (int) mono.size() - 4 * maxPeriod - hop - 16);
    reset();
}

void GrainShifter::reset() noexcept
{
    for (auto& r : ring)
        std::fill (r.begin(), r.end(), 0.0f);
    std::fill (mono.begin(), mono.end(), 0.0f);

    totalIn = 0;
    markCount = 0;
    grainMarkIndex = 0;
    gridSpacing = 0.0;
    grainHead = grainTail = 0;
    nextCentre = 0.0;
    started = false;
}

void GrainShifter::push (const float* const* input, int numChannels) noexcept
{
    channels = std::clamp (numChannels, 1, maxChannels);
    const float scale = 1.0f / (float) channels;

    for (int i = 0; i < hop; ++i)
    {
        const auto slot = (size_t) ((totalIn + i) & ringMask);
        float sum = 0.0f;

        for (int c = 0; c < channels; ++c)
        {
            ring[c][slot] = input[c][i];
            sum += input[c][i];
        }

        mono[slot] = sum * scale;
    }

    totalIn += hop;
}

float GrainShifter::interpolate (const float* r, int mask, double position) noexcept
{
    // 4-point Catmull-Rom; exact at integer positions.
    const auto i1 = (int64_t) std::floor (position);
    const float t = (float) (position - (double) i1);
    const float y0 = r[(size_t) ((i1 - 1) & mask)], y1 = r[(size_t) (i1 & mask)];
    const float y2 = r[(size_t) ((i1 + 1) & mask)], y3 = r[(size_t) ((i1 + 2) & mask)];

    if (t <= 0.0f)
        return y1;

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

float GrainShifter::sampleAt (int channel, double position) const noexcept
{
    return interpolate (ring[channel].data(), ringMask, position);
}

float GrainShifter::monoAt (double position) const noexcept
{
    return interpolate (mono.data(), ringMask, position);
}

//==============================================================================
void GrainShifter::placeMarks (double period, bool lock) noexcept
{
    const int W = (int) std::lround (period);            // one period of correlation
    const int R = std::max (2, (int) (period / 8.0));    // search range either side
    const int64_t newest = totalIn - 1;

    if (markCount == 0)
    {
        if (totalIn < (int64_t) (W + R + 2))
            return;

        // Start on the loudest sample of the first full period, and back-fill
        // marks to before time zero so the output is complete from the start.
        const int64_t first = totalIn - W - R - 2;
        int64_t loudest = first;
        for (int64_t i = first; i < first + W; ++i)
            if (std::abs (mono[(size_t) (i & ringMask)]) > std::abs (mono[(size_t) (loudest & ringMask)]))
                loudest = i;

        const int back = (int) std::floor ((double) loudest / period) + 1;
        for (int k = back; k >= 0; --k)
            marks[(size_t) (markCount++ & markMask)] = { (double) loudest - k * period, period };

        gridSpacing = period;
    }

    float* ref = reference.data();
    float* cand = candidates.data();

    while (true)
    {
        const Mark& last = marks[(size_t) ((markCount - 1) & markMask)];

        if (lock)
        {
            // Nothing is being shifted: an evenly spaced grid (whatever spacing
            // the marks had when the shift stopped) makes the grains sum to
            // exactly the input, so the spacing is simply kept.
            const double position = last.position + gridSpacing;

            if ((int64_t) std::ceil (position) + R > newest)
                break;

            marks[(size_t) (markCount & markMask)] = { position, gridSpacing };
            ++markCount;
            continue;
        }

        gridSpacing = last.spacing;
        const double predicted = last.position + period;
        const auto c0 = (int64_t) std::lround (predicted);

        if (c0 + R > newest)
            break;

        // Reference: the period ending at the last mark (read at its fractional
        // position). Candidates: periods ending at c0 - R .. c0 + R.
        const int64_t base = c0 - R - W + 1;
        const int candLength = W + 2 * R;

        double refEnergy = 0.0;
        for (int i = 0; i < W; ++i)
        {
            ref[i] = monoAt (last.position - W + 1 + i);
            refEnergy += (double) ref[i] * ref[i];
        }

        for (int i = 0; i < candLength; ++i)
            cand[i] = mono[(size_t) ((base + i) & ringMask)];

        int best = R;
        double frac = 0.0;
        bool matched = false;

        if (refEnergy > 1.0e-12)
        {
            // Coarse pass on decimated windows narrows the search for long periods.
            int from = 0, to = 2 * R;
            const int stride = std::clamp (W / 384, 1, 4);

            if (stride > 1)
            {
                float* refC = referenceCoarse.data();
                float* candC = candidatesCoarse.data();
                const int wC = W / stride, candCLength = candLength / stride;

                for (int i = 0; i < wC; ++i)
                    refC[i] = ref[i * stride];
                for (int i = 0; i < candCLength; ++i)
                    candC[i] = cand[i * stride];

                double bestScore = -2.0;
                int bestK = R / stride;

                for (int k = 0; k + wC <= candCLength; ++k)
                {
                    double num = 0.0, energy = 0.0;
                    for (int i = 0; i < wC; ++i)
                    {
                        num += (double) candC[k + i] * refC[i];
                        energy += (double) candC[k + i] * candC[k + i];
                    }

                    const double score = energy > 1.0e-12 ? num / std::sqrt (energy * refEnergy) : -1.0;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestK = k;
                    }
                }

                from = std::max (0, bestK * stride - stride + 1);
                to = std::min (2 * R, bestK * stride + stride - 1);
            }

            double bestScore = -2.0;
            for (int d = from; d <= to; ++d)
            {
                double num = 0.0, energy = 0.0;
                const float* c = cand + d;
                for (int i = 0; i < W; ++i)
                {
                    num += (double) c[i] * ref[i];
                    energy += (double) c[i] * c[i];
                }

                const double score = energy > 1.0e-12 ? num / std::sqrt (energy * refEnergy) : -1.0;
                scores[(size_t) (d - from)] = (float) score;

                if (score > bestScore)
                {
                    bestScore = score;
                    best = d;
                }
            }

            matched = bestScore >= 0.3;

            if (matched && best > from && best < to)
            {
                const double a = scores[(size_t) (best - 1 - from)], b = scores[(size_t) (best - from)], c = scores[(size_t) (best + 1 - from)];
                const double denom = a - 2.0 * b + c;
                if (denom < -1.0e-12)
                    frac = std::clamp (0.5 * (a - c) / denom, -0.5, 0.5);
            }
        }

        // Unmatched (silence, noise, a transient): keep the marks evenly spaced.
        const double position = matched ? (double) (c0 - R + best) + frac : predicted;
        marks[(size_t) (markCount & markMask)] = { position, std::clamp (position - last.position, (double) minPeriod, (double) maxPeriod) };
        ++markCount;
    }
}

//==============================================================================
void GrainShifter::render (const Controls& controls, float* const* output, int numChannels) noexcept
{
    const int numCh = std::clamp (numChannels, 1, channels);

    for (int c = 0; c < numCh; ++c)
        std::fill (output[c], output[c] + hop, 0.0f);

    const double period = std::clamp ((double) controls.analysisPeriod, (double) minPeriod, (double) maxPeriod);
    placeMarks (period, controls.lockToInput);

    if (markCount == 0)
        return;

    const int64_t start = totalIn - delay;
    const int64_t blockEnd = start + hop - 1;
    const double outputPeriod = std::clamp ((double) controls.outputPeriod, 4.0, 32.0 * maxPeriod);
    const double rate = std::clamp ((double) controls.formantRatio, 1.0 / 16.0, 16.0);
    const int64_t oldestMark = std::max<int64_t> (0, markCount - (markMask + 1));

    if (! started)
    {
        nextCentre = marks[0].position;
        grainMarkIndex = 0;
        started = true;
    }

    // Create the grains that begin inside this block.
    while (grainHead - grainTail <= grainMask)
    {
        // Source: the latest mark at or before the centre.
        grainMarkIndex = std::max (grainMarkIndex, oldestMark);
        while (grainMarkIndex + 1 < markCount && marks[(size_t) ((grainMarkIndex + 1) & markMask)].position <= nextCentre + 1.0e-6)
            ++grainMarkIndex;

        const Mark& mark = marks[(size_t) (grainMarkIndex & markMask)];
        const double spacing = mark.spacing > 0.0 ? mark.spacing : period;
        const double halfIn = spacing * (controls.lockToInput ? 1.0 : std::clamp ((double) controls.grainPeriods, 0.25, 1.0));
        const double halfOut = std::min (halfIn / rate, 1.5 * spacing);

        if (nextCentre - halfOut > (double) blockEnd)
            break;

        Grain g;
        g.centre = nextCentre;
        g.source = mark.position;
        g.halfLength = halfOut;
        g.rate = rate;
        g.gain = (float) ((controls.lockToInput ? spacing : outputPeriod) / halfOut);
        grains[(size_t) (grainHead & grainMask)] = g;
        ++grainHead;

        if (controls.lockToInput)
        {
            // On a mark already: step to the next one, so the grains sum to
            // exactly the input. Otherwise slide towards the marks gently
            // (at most half a percent of a period per period, inaudible).
            const bool onMark = std::abs (nextCentre - mark.position) < 1.0e-6;
            const double predicted = nextCentre + spacing;
            double nearest = predicted, nearestDistance = 1.0e30;
            bool found = false;

            for (int64_t m = grainMarkIndex; m < markCount; ++m)
            {
                const double p = marks[(size_t) (m & markMask)].position;
                if (p > predicted + 0.5 * spacing)
                    break;
                if (p > nextCentre + 1.0e-6 && std::abs (p - predicted) < nearestDistance)
                {
                    nearest = p;
                    nearestDistance = std::abs (p - predicted);
                    found = true;
                }
            }

            if (! found)
                nextCentre = predicted;
            else if (onMark && grainMarkIndex + 1 < markCount)
                nextCentre = marks[(size_t) ((grainMarkIndex + 1) & markMask)].position;
            else
            {
                const double offset = nearest - predicted;
                const double limit = 0.005 * spacing;
                nextCentre = std::abs (offset) <= limit ? nearest : predicted + std::clamp (offset, -limit, limit);
            }
        }
        else
        {
            nextCentre += outputPeriod;
        }
    }

    // Add every active grain's share of this block, then retire finished ones.
    for (int64_t gi = grainTail; gi < grainHead; ++gi)
    {
        const Grain& g = grains[(size_t) (gi & grainMask)];
        const auto from = std::max (start, (int64_t) std::ceil (g.centre - g.halfLength));
        const auto to = std::min (blockEnd, (int64_t) std::floor (g.centre + g.halfLength));
        const double phaseStep = kPi / g.halfLength;

        for (int64_t j = from; j <= to; ++j)
        {
            const double u = (double) j - g.centre;
            const float w = g.gain * (0.5f + 0.5f * (float) std::cos (phaseStep * u));
            const double pos = g.source + u * g.rate;
            const auto slot = (size_t) (j - start);

            for (int c = 0; c < numCh; ++c)
                output[c][slot] += w * sampleAt (c, pos);
        }
    }

    while (grainTail < grainHead)
    {
        const Grain& g = grains[(size_t) (grainTail & grainMask)];
        if (g.centre + g.halfLength >= (double) (blockEnd + 1))
            break;
        ++grainTail;
    }
}

#if TETHER_GRAIN_DEBUG
void GrainShifter::debugDump (FILE* f) const
{
    const int64_t from = std::max<int64_t> (0, markCount - 40);
    for (int64_t m = from; m < markCount; ++m)
        std::fprintf (f, "mark %lld pos %.3f spacing %.3f\n", (long long) m, marks[(size_t) (m & markMask)].position, marks[(size_t) (m & markMask)].spacing);
    for (int64_t gi = grainTail; gi < grainHead; ++gi)
    {
        const Grain& g = grains[(size_t) (gi & grainMask)];
        std::fprintf (f, "grain %lld centre %.3f source %.3f half %.3f rate %.3f gain %.3f\n", (long long) gi, g.centre, g.source, g.halfLength, g.rate, g.gain);
    }
    std::fprintf (f, "totalIn %lld nextCentre %.3f gridSpacing %.3f\n", (long long) totalIn, nextCentre, gridSpacing);
}
#endif

} // namespace tether
