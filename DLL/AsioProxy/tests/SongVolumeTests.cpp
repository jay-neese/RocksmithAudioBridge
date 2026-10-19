#include "../../SongVolume/SongVolumeDsp.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

static void Check(bool value, const char* why)
{
	if (!value) { std::cerr << why << '\n'; std::exit(1); }
}

int main()
{
	using namespace Audio::SongVolume;
	Check(MatchesSong(48000, 9000000, 48000, 9000000), "Selected backing track did not match");
	Check(!MatchesSong(48000, 9000000, 48000, 3000000), "An unrelated stream matched the song");
	Check(!MatchesSong(48000, 9000000, 44100, 9000000), "An unrelated sample rate matched the song");
	Check(!MatchesSong(0, 0, 48000, 0), "Empty measurement matched");
	const float loud = CalculateGain(0.2f, 0.8f, -200);
	const float quiet = CalculateGain(0.05f, 0.2f, -200);
	Check(std::abs(loud * 0.2f - quiet * 0.05f) < 1e-6f, "Different tracks did not reach the same target");
	Check(std::abs(loud - 0.5f) < 1e-5f && std::abs(quiet - 2.0f) < 1e-5f, "Wrong per-song correction");
	Check(CalculateGain(0.01f, 1.0f, -120) < 0.892f, "Measured peak headroom was exceeded");
	Check(CalculateGain(0.0001f, 0.001f, -120) <= 3.982f, "Boost bound exceeded");
	Check(CalculateGain(0, 0, -200) == 1, "Silence was boosted");
	Check(CalculateGain(std::numeric_limits<float>::quiet_NaN(), 0.5f, -200) == 1, "Invalid analysis applied");
	// Reproduce installed songs: authored bank gain is applied AFTER our PCM correction.
	// Equal decoder RMS alone previously left How's It Going to Be 3 dB above Seed.
	const float howsRms = 0.273021f, seedRms = 0.306670f;
	const float howsGain = CalculateGain(howsRms, 1, -140, true, -9);
	const float seedGain = CalculateGain(seedRms, 1, -140, true, -12);
	const float howsOutput = howsRms * howsGain * std::pow(10.0f, -9.0f / 20);
	const float seedOutput = seedRms * seedGain * std::pow(10.0f, -12.0f / 20);
	Check(std::abs(howsOutput - seedOutput) < 1e-6f, "Soundbank gain left tracks at different final levels");
	Check(std::abs(20 * std::log10(howsGain) + 5.72f) < 0.02f, "How's It Going to Be bank correction incorrect");
	// Different recordings AND different bank gains reach a common downstream target.
	for (float bank : { -6.0f, -8.0f, -9.0f, -12.0f, -18.0f, 0.0f, 0.5f })
	{
		const float correction = CalculateGain(0.2f, 0.4f, -200, true, bank);
		const float output = 0.2f * correction * std::pow(10.0f, bank / 20);
		Check(std::abs(output - 0.1f * std::pow(10.0f, ReferenceBankDb / 20)) < 1e-6f,
			"Authored bank volume was not canceled");
		Check(correction * 0.4f <= 0.891251f, "Bank compensation exceeded decoder peak headroom");
	}
	Check(CalculateGain(0.01f, 1, -120, true, -40) <= 0.891251f, "Quiet bank bypassed peak protection");
	Check(CalculateGain(0.01f, 1, -120, false, -40) <= 3.982f, "Bank compensation exceeded gain bound");
	Check(CalculateGain(0.2f, 1, -140, true, std::numeric_limits<float>::quiet_NaN()) == 1,
		"Invalid soundbank volume applied");
	Check(CalculateGain(0.2f, 1, -140, true, 25) == 1, "Unsupported soundbank volume applied");
	Check(ValidBankVolume(0.5f) && ValidBankVolume(24) && !ValidBankVolume(-97), "Wrong supported bank range");
	Check(std::abs(CalculateGain(0.2f, 1, -140, true, 0.5f)
		/ CalculateGain(0.2f, 1, -140, true, 0) - std::pow(10.0f, -0.5f / 20)) < 1e-6f,
		"No Sleep Till Brooklyn's +0.5 dB bank was not compensated");
	for (float bank : { 0.5f, 6.0f, 24.0f })
	{
		const float authoredGain = std::pow(10.0f, bank / 20);
		Check(CalculateGain(0.01f, 1, -120, true, bank) * authoredGain <= 0.891251f,
			"Static peak protection ignored downstream positive bank gain");
		for (unsigned sr : { 44100u, 48000u })
		{
			// Full-scale transients, starting at unity gain, exercise protection BEFORE
			// the normalization ramp settles and while boost is active.
			for (bool boosted : { false, true })
			{
				std::vector<int16_t> positiveBank(sr / 5 * 2);
				for (unsigned f = 0; f < sr / 5; ++f)
				{
					positiveBank[f * 2] = f % 2 ? -32768 : 32767;
					positiveBank[f * 2 + 1] = positiveBank[f * 2] / 2;
				}
				const auto originalPositive = positiveBank;
				ProcessingState processor;
				const float wanted = CalculateGain(0.2f, 1, -140, !boosted, bank) * (boosted ? BoostGain(100) : 1);
				ApplySongGain(positiveBank.data(), sr / 5, sr, wanted, true, processor, bank);
				for (int16_t sample : positiveBank)
					Check(std::abs(float(sample)) * authoredGain <= 29204.01f,
						"Positive bank exceeded final peak ceiling during ramp or boost");
				auto chunked = originalPositive;
				ProcessingState chunkState;
				for (unsigned offset = 0; offset < sr / 5; )
				{
					const unsigned count = std::min(137u, sr / 5 - offset);
					ApplySongGain(chunked.data() + offset * 2, count, sr, wanted, true, chunkState, bank);
					offset += count;
				}
				Check(chunked == positiveBank, "Positive-bank limiter depends on callback size");
			}
		}
	}
	std::vector<int16_t> audio(4800 * 2, 8000);
	for (unsigned frame = 0; frame < 4800; ++frame) audio[frame * 2 + 1] = -4000;
	float gain = 1;
	ApplyGain(audio.data(), 4800, 48000, loud, gain);
	Check(gain == loud, "Correction did not settle");
	Check(audio[9598] == 4000 && audio[9599] == -2000, "Stereo balance changed");
	// A quiet passage gets exactly the same correction, rather than being raised to the target.
	int16_t passage[] = { 2000, -1000 };
	ApplyGain(passage, 1, 48000, loud, gain);
	Check(passage[0] == 1000 && passage[1] == -500, "Song dynamics changed");
	std::fill(audio.begin(), audio.end(), 8000);
	ApplyGain(audio.data(), 4800, 48000, 1, gain);
	Check(gain == 1 && audio.back() == 8000, "Disabling did not restore unity");
	int16_t bypass[] = { -32768, 32767, 0, 1 };
	ApplyGain(bypass, 2, 48000, 1, gain);
	Check(bypass[0] == -32768 && bypass[1] == 32767 && bypass[2] == 0 && bypass[3] == 1, "Bypass changed samples");
	int16_t peak[] = { -32768, 32767 }; gain = 4;
	ApplyGain(peak, 1, 48000, 4, gain);
	Check(peak[0] == -32768 && peak[1] == 32767, "Integer peak backstop wrapped samples");

	Check(std::abs(BoostGain(100) - 3.16227766f) < 1e-5f, "+10 dB boost scale incorrect");
	Check(BoostGain(-10) == 1 && BoostGain(150) == BoostGain(100), "Boost range not clamped");
	Check(CalculateGain(0.05f, 1.0f, -200, false) > 1.99f, "Live limiter did not replace the offline peak restriction");
	// Quiet music plus occasional full-scale transients must get audibly louder, not just hit a hard clip.
	for (unsigned sr : { 44100u, 48000u })
	{
		const unsigned frames = sr;
		std::vector<int16_t> boosted(frames * 2);
		for (unsigned f = 0; f < frames; ++f)
		{
			int16_t sample = static_cast<int16_t>(2600 * std::sin(6.28318530718 * 220 * f / sr));
			if (f % (sr / 10) == 0) sample = 30000;
			boosted[f * 2] = sample; boosted[f * 2 + 1] = sample / 2;
		}
		const auto original = boosted;
		ProcessingState processor;
		ApplySongGain(boosted.data(), frames, sr, BoostGain(100), true, processor);
		double originalEnergy = 0, boostedEnergy = 0;
		for (unsigned f = sr / 5; f < frames; ++f)
		{
			const int left = boosted[f * 2], right = boosted[f * 2 + 1];
			Check(std::abs(left) <= 29204 && std::abs(right) <= 29204, "Song limiter ceiling exceeded");
			Check(std::abs(left - 2 * right) <= 4, "Song limiter changed stereo balance");
			originalEnergy += double(original[f * 2]) * original[f * 2];
			boostedEnergy += double(left) * left;
		}
		Check(boostedEnergy > originalEnergy * 2, "Boost did not increase the song's average level");
		Check(processor.limiterGain < 1, "Loud transients never engaged limiter");
		// Processing a stream in varying callback sizes must produce the same result as one large block.
		auto chunked = original;
		ProcessingState chunkState;
		for (unsigned offset = 0; offset < frames; )
		{
			const unsigned count = std::min(137u, frames - offset);
			ApplySongGain(chunked.data() + offset * 2, count, sr, BoostGain(100), true, chunkState);
			offset += count;
		}
		Check(chunked == boosted, "Song limiter depends on callback size");
		std::vector<int16_t> restore(sr * 4, 2000);
		ApplySongGain(restore.data(), sr * 2, sr, 1, false, processor);
		Check(processor.gain == 1 && processor.limiterGain == 1 && !processor.limiting,
			"Turning boost off did not restore exact bypass");
		Check(restore.back() == 2000, "Turning boost off changed original level");
		int16_t exact[] = { -32768, 32767, 0, 1 };
		ApplySongGain(exact, 2, sr, 1, false, processor);
		Check(exact[0] == -32768 && exact[1] == 32767 && exact[2] == 0 && exact[3] == 1,
			"Boost-off bypass changed samples");
	}
	std::cout << "Song-only gain tests passed\n";
}
