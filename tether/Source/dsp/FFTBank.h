#pragma once

#include "DspUtils.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

namespace tether
{

/** Owns one FFT engine per power-of-two size, created up front so that
    switching resolution on the audio thread never allocates. */
class FFTBank
{
public:
    void prepare (int minOrder, int maxOrder)
    {
        for (int order = 0; order < (int) ffts.size(); ++order)
        {
            const bool wanted = order >= minOrder && order <= maxOrder;

            if (wanted && ffts[(size_t) order] == nullptr)
                ffts[(size_t) order] = std::make_unique<juce::dsp::FFT> (order);
        }
    }

    const juce::dsp::FFT* get (int size) const noexcept
    {
        const int order = log2Int (size);
        return order < (int) ffts.size() ? ffts[(size_t) order].get() : nullptr;
    }

private:
    std::array<std::unique_ptr<juce::dsp::FFT>, 17> ffts;
};

} // namespace tether
