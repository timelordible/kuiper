#include "Renderer.h"

#include <cmath>

Renderer::Renderer (int numSources, std::vector<kuiper::Speaker> speakerList, const kuiper::Vbap& vbapToUse)
    : sourceCount (numSources),
      speakers (std::move (speakerList)),
      vbap (vbapToUse),
      queue ((size_t) queueCapacity),
      sources ((size_t) numSources),
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

bool Renderer::post (const ControlMessage& message, double arrivalMs)
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 == 0)
        return false;

    queue[(size_t) start1] = { message, arrivalMs };
    fifo.finishedWrite (1);
    return true;
}

void Renderer::applyMessagesDueBy (double timeMs)
{
    for (;;)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (1, start1, size1, start2, size2);
        if (size1 == 0 || queue[(size_t) start1].arrivalMs > timeMs)
            return;

        apply (queue[(size_t) start1].message);
        fifo.finishedRead (1);
    }
}

void Renderer::apply (const ControlMessage& m)
{
    if (m.type == ControlMessage::Type::masterGain)
    {
        masterGain = m.value;
        return;
    }

    if (m.source < 0 || m.source >= sourceCount)
        return;

    auto& source = sources[(size_t) m.source];

    switch (m.type)
    {
        case ControlMessage::Type::direction:  source.target = m.direction; break;
        case ControlMessage::Type::gain:       source.gain = m.value; break;
        case ControlMessage::Type::mute:       source.mute = m.value != 0.0f; break;
        case ControlMessage::Type::masterGain: break;
    }
}

void Renderer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate());
}

void Renderer::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                 float* const* outputs, int numOutputs,
                                                 int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&)
{
    process (inputs, numInputs, outputs, numOutputs, numSamples, juce::Time::getMillisecondCounterHiRes());
}

void Renderer::process (const float* const* inputs, int numInputs,
                        float* const* outputs, int numOutputs,
                        int numSamples, double callbackStartMs)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (numSamples <= 0)
        return;

    // Messages that arrived during the previous buffer are replayed across this one,
    // so they keep their spacing at the cost of one buffer of latency
    const auto windowStartMs = previousCallbackMs < 0.0 ? callbackStartMs : previousCallbackMs;
    const auto windowMs = callbackStartMs - windowStartMs;
    previousCallbackMs = callbackStartMs;

    const auto tauSamples = (double) smoothingMs.load() * 0.001 * sampleRate;
    const auto numSpeakers = speakers.size();
    const auto numActiveSources = juce::jmin (sourceCount, numInputs);

    for (int frameStart = 0; frameStart < numSamples; frameStart += controlFrameSize)
    {
        const auto frameLength = juce::jmin (controlFrameSize, numSamples - frameStart);

        applyMessagesDueBy (windowMs > 0.0 ? windowStartMs + windowMs * frameStart / numSamples
                                           : callbackStartMs);

        // One-pole smoothing of each position, once per frame
        const auto coeff = tauSamples > 1.0 ? (float) (1.0 - std::exp (-(double) frameLength / tauSamples)) : 1.0f;

        for (int s = 0; s < numActiveSources; ++s)
        {
            const auto* in = inputs[s];
            if (in == nullptr)
                continue;

            in += frameStart;
            auto& source = sources[(size_t) s];
            auto& d = source.smoothed;
            d.x += coeff * (source.target.x - d.x);
            d.y += coeff * (source.target.y - d.y);
            d.z += coeff * (source.target.z - d.z);

            vbap.computeGains (d, targetGains.data());

            const auto sourceGain = source.mute ? 0.0f : source.gain * masterGain;
            auto* current = currentGains.data() + (size_t) s * numSpeakers;

            for (size_t k = 0; k < numSpeakers; ++k)
            {
                const auto ch = speakers[k].outputChannel;
                const auto from = current[k];
                const auto to = ch >= 0 ? targetGains[k] * sourceGain : 0.0f;  // imaginary speakers are dropped
                current[k] = to;

                if (ch < 0 || ch >= numOutputs || outputs[ch] == nullptr || (from == 0.0f && to == 0.0f))
                    continue;

                // Linear ramp from the last frame's gain so changes don't click
                auto* out = outputs[ch] + frameStart;
                const auto step = (to - from) / (float) frameLength;
                for (int n = 0; n < frameLength; ++n)
                    out[n] += in[n] * (from + step * (float) (n + 1));
            }
        }
    }
}
