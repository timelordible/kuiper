// Control-frame checks for the renderer, driven with fake buffers and fake times,
// so no audio device is needed. Plain asserts, no framework.

#include "Renderer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace kuiper;

namespace
{
    constexpr float pi = 3.14159265358979f;
    constexpr double sampleRate = 48000.0;
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            std::printf ("FAIL: %s\n", what.c_str());
            ++failures;
        }
    }

    bool near (float a, float b) { return std::abs (a - b) < 1.0e-3f; }

    // 8 speakers at ear height, 45 deg apart: speaker i is on output i
    std::vector<Speaker> ringLayout()
    {
        std::vector<Speaker> layout;
        for (int i = 0; i < 8; ++i)
            layout.push_back ({ (float) (i * 45), 0.0f, false, i });
        return layout;
    }

    ControlMessage towards (float azimuthDeg)
    {
        return { ControlMessage::Type::direction, 0, directionFromAzEl (azimuthDeg * pi / 180.0f, 0.0f) };
    }

    // One source fed with a constant 1, so each output sample is that speaker's gain
    struct Rig
    {
        explicit Rig (int bufferSize)
            : input ((size_t) bufferSize, 1.0f), outputs (8, std::vector<float> ((size_t) bufferSize))
        {
            std::string error;
            check (vbap.prepare (ringLayout(), error), "prepare: " + error);
            renderer = std::make_unique<Renderer> (1, ringLayout(), vbap);
            renderer->setMasterGain (1.0f);
            renderer->setSmoothingMs (0.0f);  // positions jump, so the frame timing is visible
            renderer->prepare (sampleRate);
        }

        void run (double callbackStartMs)
        {
            const float* in[] = { input.data() };
            float* out[8];
            for (size_t ch = 0; ch < 8; ++ch)
                out[ch] = outputs[ch].data();
            renderer->process (in, 1, out, 8, (int) input.size(), callbackStartMs);
        }

        float at (int channel, int sample) const { return outputs[(size_t) channel][(size_t) sample]; }

        Vbap vbap;
        std::unique_ptr<Renderer> renderer;
        std::vector<float> input;
        std::vector<std::vector<float>> outputs;
    };

    void messagesKeepTheirTimingInsideABuffer()
    {
        // 1024 samples at 48 kHz = 21.33 ms per callback
        Rig rig (1024);
        const auto bufferMs = 1024.0 * 1000.0 / sampleRate;

        rig.run (0.0);  // source starts at the front (speaker 0)
        check (near (rig.at (0, 1023), 1.0f), "starts at the front speaker");

        // Two moves during the second buffer: to 45 deg at 1 ms, to 90 deg at 11 ms.
        // They are replayed during the third buffer at the same spacing: frames from sample 64 and 576.
        rig.renderer->post (towards (45.0f), bufferMs + 1.0);
        rig.renderer->post (towards (90.0f), bufferMs + 11.0);
        rig.run (bufferMs);
        check (near (rig.at (0, 1023), 1.0f), "moves wait one buffer");

        rig.run (2.0 * bufferMs);
        check (near (rig.at (0, 63), 1.0f), "first frame still at the front");
        check (near (rig.at (1, 127), 1.0f) && near (rig.at (0, 127), 0.0f), "45 deg reached in the second frame");
        check (near (rig.at (1, 575), 1.0f), "still at 45 deg just before the second move");
        check (near (rig.at (2, 639), 1.0f) && near (rig.at (1, 639), 0.0f), "90 deg reached in frame 576-639");
        check (near (rig.at (2, 1023), 1.0f), "ends at 90 deg");
    }

    void gainsChangeOnlyWithinFrames()
    {
        // A jump ramps over one 64-sample frame, not over the whole 1024-sample buffer
        Rig rig (1024);
        rig.run (0.0);
        rig.renderer->post (towards (90.0f), 0.0);
        rig.run (1.0);  // a 1 ms window: the message is due from the first frame

        check (near (rig.at (2, 63), 1.0f), "jump completes by the end of the first frame");
        check (rig.at (2, 31) > 0.4f && rig.at (2, 31) < 0.6f, "jump ramps linearly across the frame");
        check (near (rig.at (2, 64), 1.0f) && near (rig.at (2, 1023), 1.0f), "gain then holds");
    }

    void smoothingIsIndependentOfBufferSize()
    {
        // The same 20 ms move gets the same position after 1024 samples, in buffers of 256 or 1024
        float result[2] = {};
        const int sizes[2] = { 256, 1024 };

        for (int i = 0; i < 2; ++i)
        {
            Rig rig (sizes[i]);
            rig.renderer->setSmoothingMs (20.0f);
            rig.run (0.0);
            rig.renderer->post (towards (90.0f), 0.0);

            const auto bufferMs = sizes[i] * 1000.0 / sampleRate;
            for (int b = 1; b <= 1024 / sizes[i]; ++b)
                rig.run (b * bufferMs);

            result[i] = rig.at (2, sizes[i] - 1);
        }

        check (std::abs (result[0] - result[1]) < 1.0e-3f,
               "smoothing differs by buffer size: " + std::to_string (result[0]) + " vs " + std::to_string (result[1]));
    }

    void gainMuteAndMaster()
    {
        Rig rig (256);
        rig.run (0.0);
        rig.renderer->post ({ ControlMessage::Type::gain, 0, {}, 0.5f }, 0.0);
        rig.run (1.0);
        check (near (rig.at (0, 255), 0.5f), "source gain");

        rig.renderer->post ({ ControlMessage::Type::masterGain, 0, {}, 0.5f }, 1.0);
        rig.run (2.0);
        check (near (rig.at (0, 255), 0.25f), "master gain");

        rig.renderer->post ({ ControlMessage::Type::mute, 0, {}, 1.0f }, 2.0);
        rig.run (3.0);
        check (near (rig.at (0, 255), 0.0f), "mute");
    }

    void fullQueueDropsInsteadOfBlocking()
    {
        Rig rig (256);
        int accepted = 0;
        for (int i = 0; i < Renderer::queueCapacity + 10; ++i)
            accepted += rig.renderer->post (towards (90.0f), 0.0) ? 1 : 0;

        check (accepted < Renderer::queueCapacity + 10 && accepted >= Renderer::queueCapacity - 1, "queue capacity");

        rig.run (1.0);
        check (near (rig.at (2, 255), 1.0f), "queued messages are applied after a full queue");
        check (rig.renderer->post (towards (0.0f), 1.0), "queue accepts again once drained");
    }
}

int main()
{
    messagesKeepTheirTimingInsideABuffer();
    gainsChangeOnlyWithinFrames();
    smoothingIsIndependentOfBufferSize();
    gainMuteAndMaster();
    fullQueueDropsInsteadOfBlocking();

    if (failures == 0)
        std::printf ("All renderer tests passed\n");

    return failures == 0 ? 0 : 1;
}
