#include "croftech/audio/Mixer.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

constexpr std::uint32_t kRate = 1000;

// A sound of frames frames, every sample value.
ct::Sound constant(std::size_t frames, float value = 0.5f, std::uint32_t rate = kRate) {
    ct::Sound sound;
    sound.sampleRate = rate;
    sound.channels = 1;
    sound.samples.assign(frames, value);
    return sound;
}

std::vector<float> mixFrames(ct::Mixer& mixer, std::size_t frames) {
    std::vector<float> out(frames * 2);
    mixer.mix(out);
    return out;
}

} // namespace

TEST_CASE("a sound plays once in both ears and its voice ends") {
    ct::Mixer mixer(kRate);
    const ct::Mixer::SoundId sound = mixer.addSound(constant(100));
    const ct::Mixer::VoiceId voice = mixer.play(sound, {});
    CHECK(mixer.isPlaying(voice));
    const std::vector<float> out = mixFrames(mixer, 150);
    CHECK(out[0] == 0.5f);
    CHECK(out[1] == 0.5f);
    CHECK(out[2 * 99] == 0.5f);
    CHECK(out[2 * 100] == 0.0f);
    CHECK_FALSE(mixer.isPlaying(voice));
    CHECK(mixer.playingCount() == 0);
}

TEST_CASE("looping sounds go round until stopped") {
    ct::Mixer mixer(kRate);
    ct::Mixer::Play loop;
    loop.loop = true;
    const ct::Mixer::VoiceId voice = mixer.play(mixer.addSound(constant(10)), loop);
    const std::vector<float> out = mixFrames(mixer, 95);
    CHECK(out[2 * 94] == 0.5f);
    CHECK(mixer.isPlaying(voice));
    mixer.stop(voice);
    CHECK_FALSE(mixer.isPlaying(voice));
    CHECK(mixFrames(mixer, 10)[0] == 0.0f);
}

TEST_CASE("placed sounds come from their side and fade with distance") {
    ct::Mixer mixer(kRate);
    const ct::Mixer::SoundId sound = mixer.addSound(constant(1000, 1.0f));
    mixer.setListener({0.0f, 0.0f, 0.0f}, 0.0f); // facing +X; +Y is on the left
    ct::Mixer::Play left;
    left.position = ct::Vec3{0.0f, 5.0f, 0.0f};
    left.reach = 20.0f;
    mixer.play(sound, left);
    std::vector<float> out = mixFrames(mixer, 4);
    CHECK(out[0] > 0.4f);
    CHECK(out[1] < 0.01f);
    // (1 - 5/20)^2 of full volume, all in the left ear.
    CHECK(out[0] == doctest::Approx(0.5625f).epsilon(0.01));

    // Turning round puts it on the right.
    mixer.setListener({0.0f, 0.0f, 0.0f}, ct::kPi);
    out = mixFrames(mixer, 64);
    CHECK(out[2 * 63] < 0.01f);
    CHECK(out[2 * 63 + 1] > 0.4f);

    ct::Mixer::Play far;
    far.position = ct::Vec3{100.0f, 0.0f, 0.0f};
    far.reach = 50.0f;
    ct::Mixer quiet(kRate);
    quiet.play(quiet.addSound(constant(100, 1.0f)), far);
    out = mixFrames(quiet, 4);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 0.0f);
}

TEST_CASE("the master volume scales everything and the mix is clipped") {
    ct::Mixer mixer(kRate);
    const ct::Mixer::SoundId sound = mixer.addSound(constant(100, 0.8f));
    mixer.play(sound, {});
    mixer.play(sound, {});
    CHECK(mixFrames(mixer, 1)[0] == 1.0f);
    // Changes ramp in over one mix, so they don't click.
    mixer.setVolume(0.25f);
    mixFrames(mixer, 64);
    CHECK(mixFrames(mixer, 1)[0] == doctest::Approx(0.4f));
}

TEST_CASE("when every voice is busy the oldest sound that ends by itself makes way") {
    ct::Mixer mixer(kRate);
    const ct::Mixer::SoundId sound = mixer.addSound(constant(1000));
    ct::Mixer::Play loop;
    loop.loop = true;
    const ct::Mixer::VoiceId looping = mixer.play(sound, loop);
    const ct::Mixer::VoiceId oldest = mixer.play(sound, {});
    for (std::size_t i = 2; i < ct::Mixer::kMaxVoices; ++i) {
        mixer.play(sound, {});
    }
    CHECK(mixer.playingCount() == ct::Mixer::kMaxVoices);
    const ct::Mixer::VoiceId newest = mixer.play(sound, {});
    CHECK(mixer.isPlaying(newest));
    CHECK(mixer.isPlaying(looping));
    CHECK_FALSE(mixer.isPlaying(oldest));
    // A stale id doesn't reach the new sound in its slot.
    mixer.stop(oldest);
    CHECK(mixer.isPlaying(newest));
}

TEST_CASE("sounds are converted to the output rate") {
    ct::Mixer mixer(kRate * 2);
    mixer.play(mixer.addSound(constant(100)), {});
    const std::vector<float> out = mixFrames(mixer, 250);
    CHECK(out[2 * 195] == 0.5f);
    CHECK(out[2 * 201] == 0.0f);
    CHECK(mixer.play(42, {}) == ct::Mixer::kNoVoice);
}
