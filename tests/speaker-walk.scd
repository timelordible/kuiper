// Speaker walk for Couch 204: plays a pink noise burst on one speaker at a time through
// Kuiper, using source 0 and OSC. It prints each speaker number, so a burst from the
// wrong place shows a channel order or azimuth problem. Start this before Kuiper, which
// needs the SuperCollider JACK client to exist. Stop it with Ctrl+C.
//
//   pw-jack sclang tests/speaker-walk.scd

var speakers = [
    [22.5, 0], [-22.5, 0], [-68, 0], [-112, 0], [-157.5, 0], [157.5, 0], [112, 0], [68, 0],
    [0, 19.2], [-90, 27.5], [180, 19.2], [90, 27.5], [0, 90]
];
var kuiper = NetAddr("127.0.0.1", 6666);

// Only Kuiper should hear this, so SuperCollider must not connect itself to the speakers
"SC_JACK_DEFAULT_OUTPUTS".setenv("");

s.options.numInputBusChannels = 0;
s.options.numOutputBusChannels = 2;

s.waitForBoot {
    var noise = { |amp = 0| Out.ar(0, PinkNoise.ar(amp.lag(0.02))) }.play;
    s.sync;
    "Speaker walk ready, start Kuiper now".postln;

    loop {
        speakers.do { |speaker, i|
            kuiper.sendMsg("/source/azim", 0, speaker[0].degrad);
            kuiper.sendMsg("/source/elev", 0, speaker[1].degrad);
            0.1.wait;
            ("Speaker " ++ (i + 1) ++ ": az " ++ speaker[0] ++ ", el " ++ speaker[1]).postln;
            noise.set(\amp, 0.1);
            1.wait;
            noise.set(\amp, 0);
            0.5.wait;
        };
    };
};
