#include "Renderer.h"

#include <cmath>

Renderer::Renderer (int numSources, std::vector<kuiper::Speaker> speakerList, const kuiper::Vbap& vbapToUse)
    : sourceCount (numSources),
      speakers (std::move (speakerList)),
      vbap (vbapToUse),
      params (std::make_unique<SourceParams[]> ((size_t) numSources)),
      smoothedDirection ((size_t) numSources, kuiper::Vec3 { 1.0f, 0.0f, 0.0f }),
      currentGains ((size_t) numSources * speakers.size(), 0.0f),
      targetGains (speakers.size(), 0.0f)
{
}

int Renderer::numOutputChannelsNeeded() const
{
    int highest = -1;
    for (const auto& s : speakers)
        highest = juce::jmax (highest, s.outputChannel);
    return highest + 1;
}

void Renderer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
}

void Renderer::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                 float* const* outputs, int numOutputs,
                                                 int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (numSamples <= 0)
        return;

    // One-pole smoothing of each direction, applied once per block
    const auto tauSamples = (double) smoothingMs.load() * 0.001 * sampleRate;
    const auto coeff = tauSamples > 1.0 ? (float) (1.0 - std::exp (-(double) numSamples / tauSamples)) : 1.0f;
    const auto master = masterGain.load();
    const auto numSpeakers = speakers.size();

    for (int s = 0; s < juce::jmin (sourceCount, numInputs); ++s)
    {
        const auto* in = inputs[s];
        if (in == nullptr)
            continue;

        auto& p = params[(size_t) s];
        auto& d = smoothedDirection[(size_t) s];
        d.x += coeff * (p.x.load() - d.x);
        d.y += coeff * (p.y.load() - d.y);
        d.z += coeff * (p.z.load() - d.z);

        vbap.computeGains (d, targetGains.data());

        const auto sourceGain = p.mute.load() ? 0.0f : p.gain.load() * master;
        auto* current = currentGains.data() + (size_t) s * numSpeakers;

        for (size_t k = 0; k < numSpeakers; ++k)
        {
            const auto ch = speakers[k].outputChannel;
            const auto from = current[k];
            const auto to = ch >= 0 ? targetGains[k] * sourceGain : 0.0f;  // imaginary speakers are dropped
            current[k] = to;

            if (ch < 0 || ch >= numOutputs || outputs[ch] == nullptr || (from == 0.0f && to == 0.0f))
                continue;

            // Linear ramp from the last block's gain so position changes don't click
            auto* out = outputs[ch];
            const auto step = (to - from) / (float) numSamples;
            for (int n = 0; n < numSamples; ++n)
                out[n] += in[n] * (from + step * (float) (n + 1));
        }
    }
}
