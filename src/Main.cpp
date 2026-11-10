// kuiper: headless VBAP renderer, the planned replacement for litespat.
//
//   kuiper --config room.json      run until SIGINT/SIGTERM
//   kuiper --config room.json --check
//                                             load the config and layout, print the triangulation, exit
//   kuiper --list-devices          print audio device types and devices, exit

#include "Config.h"
#include "Layout.h"
#include "OscServer.h"
#include "Renderer.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <csignal>
#include <iostream>

namespace
{
    std::atomic<bool> quitRequested { false };

    void onSignal (int) { quitRequested.store (true); }

    // Signal handlers can't touch the message loop, so poll a flag from it instead
    struct QuitPoller : juce::Timer
    {
        void timerCallback() override
        {
            if (quitRequested.load())
                juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    };

    int fail (const juce::String& message)
    {
        std::cerr << "kuiper: " << message << "\n";
        return 1;
    }

    void listDevices()
    {
        juce::AudioDeviceManager manager;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        manager.createAudioDeviceTypes (types);

        for (auto* type : types)
        {
            type->scanForDevices();
            std::cout << type->getTypeName() << "\n";
            for (const auto& name : type->getDeviceNames (false))
                std::cout << "  out: " << name << "\n";
            for (const auto& name : type->getDeviceNames (true))
                std::cout << "  in:  " << name << "\n";
        }
    }

    std::unique_ptr<juce::AudioIODevice> openDevice (const Config& config, int numOutputs, juce::String& error,
                                                     juce::OwnedArray<juce::AudioIODeviceType>& types)
    {
        juce::AudioDeviceManager manager;
        manager.createAudioDeviceTypes (types);

        juce::AudioIODeviceType* type = nullptr;
        for (auto* t : types)
            if (config.deviceType.isEmpty() || t->getTypeName().equalsIgnoreCase (config.deviceType))
            {
                type = t;
                break;
            }

        if (type == nullptr)
        {
            error = "audio device type not available: " + config.deviceType;
            return {};
        }

        type->scanForDevices();

        auto pick = [type] (const juce::String& wanted, bool input)
        {
            if (wanted.isNotEmpty())
                return wanted;
            const auto names = type->getDeviceNames (input);
            return names[juce::jmax (0, type->getDefaultDeviceIndex (input))];
        };

        std::unique_ptr<juce::AudioIODevice> device (type->createDevice (pick (config.outputDevice, false),
                                                                         pick (config.inputDevice, true)));
        if (device == nullptr)
        {
            error = "could not create the audio device";
            return {};
        }

        const auto availableIn = device->getInputChannelNames().size();
        const auto availableOut = device->getOutputChannelNames().size();

        if (availableOut < numOutputs)
        {
            error = "the layout needs " + juce::String (numOutputs) + " outputs but "
                    + device->getName() + " has " + juce::String (availableOut);
            return {};
        }

        if (availableIn < config.numInputs)
            std::cerr << "Warning: " << config.numInputs << " inputs configured, device has " << availableIn << "\n";

        juce::BigInteger inputs, outputs;
        inputs.setRange (0, juce::jmin (config.numInputs, availableIn), true);
        outputs.setRange (0, numOutputs, true);

        std::cout << "Opening " << type->getTypeName() << " device " << device->getName() << std::endl;
        error = device->open (inputs, outputs, config.sampleRate, config.bufferSize);
        if (error.isNotEmpty())
            return {};

        return device;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;  // message loop only, no windows

    juce::File configFile;
    bool checkOnly = false;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--config" && i + 1 < argc)
            configFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else if (arg == "--check")
            checkOnly = true;
        else if (arg == "--list-devices")
        {
            listDevices();
            return 0;
        }
        else
            return fail ("usage: kuiper --config <file.json> [--check] | --list-devices");
    }

    if (configFile == juce::File())
        return fail ("no --config given");

    Config config;
    juce::String error;
    if (! loadConfig (configFile, config, error))
        return fail (error);

    std::vector<kuiper::Speaker> speakers;
    if (! loadIemLayout (config.layoutFile, speakers, error))
        return fail (error);

    kuiper::Vbap vbap;
    std::string vbapError;
    if (! vbap.prepare (speakers, vbapError))
        return fail ("layout " + config.layoutFile.getFileName() + ": " + juce::String (vbapError));

    Renderer renderer (config.numInputs, speakers, vbap);
    renderer.setMasterGain (juce::Decibels::decibelsToGain (config.masterGainDb));
    renderer.setSmoothingMs (config.smoothingMs);

    std::cout << "Layout " << config.layoutFile.getFileName() << ": " << speakers.size() << " speakers, "
              << vbap.numGroups() << (vbap.is2D() ? " pairs (2D)" : " triangles (3D)") << ", "
              << renderer.numOutputChannelsNeeded() << " output channels" << std::endl;

    if (checkOnly)
        return 0;

    juce::OwnedArray<juce::AudioIODeviceType> types;  // must outlive the device
    std::unique_ptr<juce::AudioIODevice> device;
    OscServer osc (renderer);
    int exitCode = 0;

    // Some device types (WASAPI) only open from inside a running message loop,
    // so startup runs as the loop's first job
    juce::MessageManager::callAsync ([&]
    {
        juce::String startError;
        device = openDevice (config, renderer.numOutputChannelsNeeded(), startError, types);

        if (device != nullptr && ! osc.start (config.oscPort))
            startError = "could not listen for OSC on port " + juce::String (config.oscPort);

        if (startError.isNotEmpty())
        {
            exitCode = fail (startError);
            quitRequested.store (true);
            return;
        }

        device->start (&renderer);
        std::cout << "Rendering " << config.numInputs << " sources on " << device->getName() << " ("
                  << device->getCurrentSampleRate() << " Hz, " << device->getCurrentBufferSizeSamples()
                  << " samples), OSC on port " << config.oscPort << std::endl;
    });

    std::signal (SIGINT, onSignal);
    std::signal (SIGTERM, onSignal);

    QuitPoller poller;
    poller.startTimer (100);
    juce::MessageManager::getInstance()->runDispatchLoop();

    if (device != nullptr)
    {
        std::cout << "Stopping" << std::endl;
        device->stop();
        device->close();
    }

    return exitCode;
}
