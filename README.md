# Kuiper

Kuiper is the planned replacement for litespat: a headless, cross-platform
renderer that pans each input straight to the speakers with VBAP. There is no HOA
encoder, no decoder and no connect script. Design outline:
https://claude.ai/artifact/T7QZ7ySNATZ53F5vYYJMLJ

Repository (private): https://github.com/timelordible/kuiper

Status: prototype skeleton (phase 2 in the outline). It renders and takes OSC.
The web server, meters, spread, distance model and DBAP are not built yet.

## Layout

```
src/core/Vbap.*         VBAP triangulation + gains, plain C++ (no JUCE)
src/app/Main.cpp        headless app: config, audio device, message loop, SIGINT/SIGTERM
src/app/Config.*        JSON config (one file per room)
src/app/Layout.*        IEM .json layout loader
src/app/Renderer.*      audio callback: smoothing, VBAP gains, gain ramps, output mapping
src/app/OscServer.*     OSC control (litespat messages + new ones)
tests/vbap_tests.cpp    gain checks from the outline's testing section
config/example.json     example config
layouts/                example layout (replace with the real room layouts from IEM)
systemd/                user service for the Ubuntu machine
```

## Build

Needs CMake 3.22+ and a C++17 compiler. JUCE 8.0.4 is downloaded at configure time.

Ubuntu (Pollux):

```bash
sudo apt install build-essential cmake git libasound2-dev libjack-jackd2-dev
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

The binary is `build/kuiper_artefacts/Release/kuiper`
(`build/kuiper_artefacts/kuiper` with single-config generators).

VBAP tests only (no JUCE download):

```bash
cmake -S . -B build-core -DKUIPER_BUILD_APP=OFF && cmake --build build-core && ctest --test-dir build-core
```

## Run

```bash
kuiper --config config/example.json --check
```

loads the config and layout, prints the triangulation and exits.

```bash
kuiper --list-devices
```

prints the audio device types and device names to put in the config.

```bash
pw-jack kuiper --config ~/.config/kuiper/config.json
```

renders until Ctrl+C or SIGTERM. On PipeWire, run it under `pw-jack` as litespat does.

## Config

```json
{
  "layout": "../layouts/example-dome-12.json",
  "inputs": 32,
  "audio": { "type": "JACK", "inputDevice": "", "outputDevice": "", "sampleRate": 48000, "bufferSize": 256 },
  "osc": { "port": 6666 },
  "smoothingMs": 20,
  "masterGainDb": -2.5
}
```

`layout` is relative to the config file. Speakers go to the output channels given by
`Channel` in the IEM layout (1-based). Imaginary speakers are used for the triangulation;
their share of the signal is dropped, as in IEM's AllRADecoder.

## OSC (UDP, default port 6666)

| Message | Arguments |
| --- | --- |
| `/source/azim` | int index, float radians (litespat) |
| `/source/elev` | int index, float radians (litespat) |
| `/source/dist` | int index, float metres (litespat; stored, not used yet) |
| `/source/aed` | int index, azim rad, elev rad, dist m |
| `/source/xyz` | int index, x, y, z (x front, y left, z up) |
| `/source/gain` | int index, float dB |
| `/source/mute` | int index, int 0/1 |
| `/master/gain` | float dB |

Indices are 0-based. Positive azimuth is to the left, as in ATK and IEM.

## Notes

- VBAP is computed directly per audio block (a few hundred multiply-adds per source),
  so there is no gain table. SAF's `saf_vbap` can replace `src/core/Vbap.cpp` later
  behind the same interface. It wasn't used yet because SAF needs a BLAS/LAPACK library to build.
- Windows: on the test laptop, opening WASAPI with the built-in microphone as input hung
  inside JUCE. Setting `"inputDevice": "none"` (output only) worked. To be checked with
  a real multichannel interface.
