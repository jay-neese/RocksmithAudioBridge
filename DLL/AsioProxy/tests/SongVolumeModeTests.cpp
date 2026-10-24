#include "../../SongVolume/SongVolume.hpp"
#include "../../SongVolume/SongVolumeMeasurement.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

static void Check(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char** argv)
{
	using namespace Audio::SongVolume;
	Configuration settings{ true, -120, 0, 0, -210 };
	const float firstRms = NormalizedGain(settings, .2f, .5f, -8, -9);
	const float secondRms = NormalizedGain(settings, .2f, .5f, -8, -13);
	Check(firstRms == secondRms && firstRms == CalculateGain(.2f, .5f, -120, true, -8), "RMS behavior changed");
	settings.mode = 1;
	const float firstLufs = NormalizedGain(settings, .2f, .5f, -8, -9);
	const float secondLufs = NormalizedGain(settings, .2f, .5f, -8, -13);
	Check(std::abs(-9 - 8 + 20 * std::log10(firstLufs) + 21) < 1e-5f, "First track missed LUFS target");
	Check(std::abs(-13 - 8 + 20 * std::log10(secondLufs) + 21) < 1e-5f, "Second track missed LUFS target");
	Check(std::abs(20 * std::log10(secondLufs / firstLufs) - 4) < 1e-5f, "LUFS did not compensate for equal-RMS spectral differences");
	for (float bank : { -13.6f, -8.f, 0.f, .5f, 7.2f, 24.f })
	{
		const float gain = CalculateLufsGain(-25, 1, -140, true, bank);
		Check(gain * std::pow(10.f, bank / 20) <= 0.891252f, "LUFS peak protection failed after bank gain");
		Check(CalculateLufsGain(-65, .1f, -140, false, bank) <= 3.982f, "LUFS boost exceeded bound");
		Check(CalculateLufsGain(5, 1, -360, false, bank) >= 0.0630957f, "LUFS cut exceeded bound");
	}
	Check(CalculateLufsGain(std::numeric_limits<float>::quiet_NaN(), 1, -210, true, -8) == 1, "NaN LUFS applied");
	Check(CalculateLufsGain(std::numeric_limits<float>::infinity(), 1, -210, true, -8) == 1, "Infinite LUFS applied");
	Check(CalculateLufsGain(-20, 0, -210, true, -8) == 1, "Zero peak accepted");
	Check(CalculateLufsGain(-71, 1, -210, true, -8) == 1, "Unmeasurable LUFS accepted");
	Check(DefaultLufsTarget(-120) == -210 && DefaultLufsTarget(-200) == -290, "Default comparison target incorrect");
	// Reproduce the user's exact problem: at -21 LUFS this song is unbounded,
	// but moving right eventually stops changing its gain until compression is allowed.
	Configuration simple{ true, -120, 0, 1, -210, false };
	const float oldBlinkGain = NormalizedGain(simple, .220757f, 1, -9.5f, -9.68088f);
	simple.compressPeaks = true;
	Check(NormalizedGain(simple, .220757f, 1, -9.5f, -9.68088f) == oldBlinkGain,
		"Compression permission added unnecessary fixed gain");
	simple.compressPeaks = false; simple.lufsTarget = -190;
	const float blinkPeakLimit = NormalizedGain(simple, .220757f, 1, -9.5f, -9.68088f);
	simple.lufsTarget = -140;
	Check(NormalizedGain(simple, .220757f, 1, -9.5f, -9.68088f) == blinkPeakLimit,
		"Peak-preserving mode did not stop at the measured peak limit");
	simple.compressPeaks = true;
	const float blinkCompressed = NormalizedGain(simple, .220757f, 1, -9.5f, -9.68088f);
	Check(std::abs(20 * std::log10(blinkCompressed) - 5.18088f) < 0.001f && blinkCompressed > blinkPeakLimit,
		"Explicit compression did not unlock blink-182's requested loudness");
	Check(simple.boost == 0 && simple.lufsTarget == -140, "Enabling compression modified the user's loudness setting");
	Configuration quietWithHeadroom{ true, -120, 0, 1, -210, false };
	const float quietGain = NormalizedGain(quietWithHeadroom, .001f, .005f, 0, -42);
	quietWithHeadroom.compressPeaks = true;
	Check(quietGain > 10 && quietGain == NormalizedGain(quietWithHeadroom, .001f, .005f, 0, -42),
		"Compression permission changed gain when no peaks needed reducing");
	const auto migrated = SimplifyConfiguration(Configuration{ true, -120, 20, 1, -140 });
	Check(migrated.mode == 1 && migrated.boost == 0 && migrated.lufsTarget == -120 && migrated.compressPeaks,
		"Old LUFS plus boost did not migrate into one target");
	Check(SimplifyConfiguration(migrated).Pack() == migrated.Pack(), "Settings were migrated twice");
	const auto migratedRms = SimplifyConfiguration(Configuration{ true, -120, 0, 0, -140 });
	Check(migratedRms.lufsTarget == -210 && migratedRms.mode == 1 && !migratedRms.compressPeaks,
		"Former RMS settings were not translated to a comparable LUFS setting");
	Check(SimplifyConfiguration(Configuration{ true, -120, 100, 1, -140 }).lufsTarget == -40,
		"Full legacy loudness range was lost");
	const auto interstateRange = CalculateLoudnessRange(-11.56083f, 1, -8, false);
	const auto blinkRange = CalculateLoudnessRange(-9.68088f, 1, -9.5f, false);
	Check(interstateRange.measured && interstateRange.adjustable && interstateRange.minimumTenths == -360 && interstateRange.maximumTenths == -206,
		"Interstate Love Song still has an unreachable upper slider range");
	Check(blinkRange.maximumTenths == -202, "Blink-182's uncompressed slider ceiling is incorrect");
	Check(CalculateLoudnessRange(-11.56083f, 1, -8, true).maximumTenths == -40,
		"Allowing compression did not unlock the wider range");
	const auto noData = CalculateLoudnessRange(std::numeric_limits<float>::quiet_NaN(), 1, -8, false);
	Check(!noData.measured && noData.adjustable, "Unknown song measurements prevented choosing a target");
	Check(!CalculateLoudnessRange(-65, 1, -96, false).adjustable, "An unreachable interval was exposed as a working slider");
	Configuration savedGoal{ true, -120, 0, 1, -140, false };
	const auto savedWord = savedGoal.Pack();
	const int displayOnly = std::clamp(savedGoal.lufsTarget, interstateRange.minimumTenths, interstateRange.maximumTenths);
	Check(displayOnly == -206 && savedGoal.Pack() == savedWord, "Showing a limited range changed the saved global target");
	// A settings word always contains a complete mode/target pair.
	for (int mode : { 0, 1 }) for (int rms : { -300, -200, -120 })
		for (int lufs : { -360, -210, -140 }) for (int boost : { 0, 100 })
		{
			Configuration original{ true, rms, boost, mode, lufs };
			const auto restored = Configuration::Unpack(original.Pack());
			Check(restored.enabled && restored.mode == mode && restored.rmsTarget == rms && restored.lufsTarget == lufs && restored.boost == boost,
				"Atomic configuration lost a mode or target");
		}
	Configuration parsed;
	Check(ParseConfiguration(L"L,1,-120,1", {}, parsed) && parsed.mode == 1 && parsed.lufsTarget == -120 && parsed.boost == 0 && parsed.compressPeaks,
		"Simplified loudness command rejected");
	Check(ParseConfiguration(L"L,1,-120,0", parsed, parsed) && !parsed.compressPeaks && parsed.lufsTarget == -120,
		"Compression off changed the loudness target");
	for (int target : { -360, -210, -140, -120, -40 }) for (bool allowed : { false, true })
	{
		Configuration c{ true, -120, 0, 1, target, allowed };
		const auto restored = Configuration::Unpack(c.Pack());
		Check(restored.lufsTarget == target && restored.compressPeaks == allowed && restored.boost == 0,
			"Atomic snapshot mixed the target and compression permission");
	}
	for (const wchar_t* bad : { L"L,1,-39,1", L"L,1,-361,0", L"L,1,-140,2", L"L,2,-140,1", L"L,1,-140", L"L,1,-140,1,0", L"L,1,-140,1junk" })
		Check(!ParseConfiguration(bad, {}, parsed), "Malformed simplified command accepted");
	Check(ParseConfiguration(L"1,-120,0,1,-210", {}, parsed) && parsed.mode == 1 && parsed.rmsTarget == -120 && parsed.lufsTarget == -210,
		"Current settings command rejected");
	Check(ParseConfiguration(L"0,-160,30", settings, parsed) && !parsed.enabled && parsed.mode == 1 && parsed.lufsTarget == -210 && parsed.boost == 30,
		"Legacy command did not preserve LUFS settings");
	Check(ParseConfiguration(L"1,-140", parsed, parsed) && parsed.mode == 1 && parsed.boost == 30, "Two-field command lost settings");
	for (const wchar_t* bad : { L"", L"1,-120,0,1", L"1,-120,0,1,-210,0", L"1,-120,0,2,-210", L"1,-120,0,1,-139",
		L"1,-120,0,1,-361", L"1,-119,0,1,-210", L"1,-120,101,1,-210", L"1,-120,0,1,-210junk", L"1,-120,0,1,-210 ", L"1,-120,0,1," })
		Check(!ParseConfiguration(bad, settings, parsed), "Malformed settings command accepted");
	Measurement measurement;
	std::istringstream valid("3 44100 11457683 .0878355158 .6601257 .5 -18.25\n");
	Check(ReadMeasurement(valid, measurement) && measurement.rate == 44100 && measurement.lufs == -18.25f, "LUFS measurement rejected");
	for (const char* bad : { "2 44100 11457683 .1 1 -8", "3 44100 11457683 .1 1 -8", "3 44100 -1 .1 1 -8 -12",
		"3 22050 11457683 .1 1 -8 -12", "3 48000 20 .1 1 -8 -12", "3 48000 11457683 .1 1 -8 -71",
		"3 48000 11457683 .1 1 -8 nan", "3 48000 11457683 .1 1 -8 -12 extra", "3 48000 11457683 .1 1.1 -8 -12" })
	{
		std::istringstream input(bad);
		Check(!ReadMeasurement(input, measurement), "Malformed/old measurement accepted");
	}
	// Live A/B switching uses the same sample ramp and stereo processing as normal
	// target changes. No intermediate target from the other mode may be selected.
	for (unsigned rate : { 44100u, 48000u })
	{
		ProcessingState state; state.gain = firstRms;
		int previous = static_cast<int>(16000 * firstRms);
		for (int mode : { 1, 0, 1, 0 })
		{
			settings.mode = mode;
			const float wanted = NormalizedGain(settings, .2f, .5f, -8, -9);
			std::vector<int16_t> audio(rate / 5 * 2);
			for (unsigned f = 0; f < rate / 5; ++f) { audio[f * 2] = 16000; audio[f * 2 + 1] = -8000; }
			ApplySongGain(audio.data(), rate / 5, rate, wanted, false, state, -8);
			for (unsigned f = 0; f < rate / 5; ++f)
			{
				Check(std::abs(audio[f * 2] - previous) <= 8, "Mode switch caused an abrupt sample step");
				Check(std::abs(audio[f * 2] + 2 * audio[f * 2 + 1]) <= 1, "Mode switch changed stereo balance");
				previous = audio[f * 2];
			}
			Check(std::abs(state.gain - wanted) < 1e-6f, "Mode switch did not settle to the selected target");
		}
	}
	// Exercise the complete LUFS + shared boost + downstream-bank protection path,
	// including its startup ramp, rather than only checking the gain formula.
	for (unsigned rate : { 44100u, 48000u }) for (float bank : { -8.f, 0.f, .5f, 7.2f })
	{
		Configuration boosted{ true, -120, 100, 1, -140 };
		const float wanted = NormalizedGain(boosted, .2f, 1, bank, -28) * BoostGain(boosted.boost);
		std::vector<int16_t> audio(rate * 2);
		for (unsigned f = 0; f < rate; ++f)
		{
			const int16_t sample = f % (rate / 10) == 0 ? 32767 : static_cast<int16_t>(5000 * std::sin(6.28318530718 * 220 * f / rate));
			audio[f * 2] = sample; audio[f * 2 + 1] = sample / 2;
		}
		ProcessingState state;
		ApplySongGain(audio.data(), rate, rate, wanted, true, state, bank);
		for (int16_t sample : audio)
			Check(std::abs(float(sample)) <= 29204.01f && std::abs(float(sample)) * std::pow(10.f, bank / 20) <= 29204.01f,
				"LUFS with +10 dB boost exceeded the song peak ceiling");
		Check(state.gain == wanted, "Boosted LUFS gain did not settle");
	}
	if (argc == 2)
	{
		unsigned checked = 0;
		for (const auto& file : std::filesystem::directory_iterator(argv[1]))
		{
			if (file.path().extension() != ".txt") continue;
			std::ifstream input(file.path()); std::string text;
			std::getline(input, text);
			if (text.rfind("3 ", 0) != 0) continue;
			std::istringstream response(text); response.imbue(std::locale::classic());
			Measurement actual;
			Check(ReadMeasurement(response, actual), "C# cache profile could not be read by native code");
			for (int mode : { 0, 1 })
			{
				Configuration comparison{ true, -120, 0, mode, -210 };
				const float gain = NormalizedGain(comparison, actual.rms, actual.peak, actual.bankDb, actual.lufs);
				Check(std::isfinite(gain) && gain > 0, "Real-song gain is invalid");
				Check(actual.peak * gain <= 0.891252f, "Real-song correction exceeded PCM headroom");
				Check(actual.peak * gain * std::pow(10.0f, actual.bankDb / 20) <= 0.891252f,
					"Real-song correction exceeded headroom after authored gain");
			}
			for (int target : { -210, -140, -40 })
			{
				Configuration compressed{ true, -120, 0, 1, target, true };
				const float wanted = NormalizedGain(compressed, actual.rms, actual.peak, actual.bankDb, actual.lufs);
				const int16_t peakSample = static_cast<int16_t>(std::min(32767L, std::lround(actual.peak * 32768)));
				int16_t transient[] = { peakSample, static_cast<int16_t>(-peakSample) };
				ProcessingState state; state.gain = wanted;
				ApplySongGain(transient, 1, actual.rate, wanted, true, state, actual.bankDb);
				Check(std::abs(float(transient[0])) <= 29204.01f
					&& std::abs(float(transient[0])) * std::pow(10.f, actual.bankDb / 20) <= 29204.01f,
					"Compression toggle failed to protect a real-song transient");
			}
			const auto reachable = CalculateLoudnessRange(actual.lufs, actual.peak, actual.bankDb, false);
			Check(reachable.measured && reachable.adjustable, "Real song unexpectedly had no usable uncompressed range");
			float previousGain = 0;
			for (int target = reachable.minimumTenths; target <= reachable.maximumTenths; ++target)
			{
				Configuration setting{ true, -120, 0, 1, target, false };
				const float currentGain = NormalizedGain(setting, actual.rms, actual.peak, actual.bankDb, actual.lufs);
				Check(currentGain > previousGain, "A displayed 0.1 LUFS slider step did not increase a real song's gain");
				previousGain = currentGain;
			}
			++checked;
		}
		Check(checked > 0, "No combined cache profiles were checked");
		std::cout << "Native protocol and loudness/compression peak checks passed on " << checked << " real-song profiles\n";
	}
	std::cout << "Song loudness and legacy-mode validation tests passed\n";
}
