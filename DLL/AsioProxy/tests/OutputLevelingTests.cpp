// Standalone DSP regression tests. No game, audio driver, or Windows APIs required.
#include "../OutputGuardDsp.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

static void Check(bool ok, const char* message)
{
	if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static void Run(OutputGuardDsp::OutputGuard& guard, float level, int blocks)
{
	constexpr int frames = 480;
	for (int block = 0; block < blocks; ++block)
	{
		float left[frames], right[frames];
		std::fill_n(left, frames, level);
		std::fill_n(right, frames, -level);
		const double energy[] = { double(level) * level, double(level) * level };
		guard.BeginBlock(energy, 2, frames);
		guard.ProcessChannel(0, left, frames);
		guard.ProcessChannel(1, right, frames);
		for (int i = 0; i < frames; ++i)
		{
			Check(std::isfinite(left[i]) && std::isfinite(right[i]), "Non-finite output");
			Check(std::abs(left[i] + right[i]) < 1e-6f, "Stereo balance changed");
		}
	}
}

int main()
{
	OutputGuardDsp::Config config;
	config.agcOn = true;
	OutputGuardDsp::OutputGuard guard;
	guard.Configure(config);
	guard.Reset(2);
	Run(guard, 0.30f, 1);
	Check(guard.AgcGain() <= 1.0f, "Startup boosted an already loud signal");
	Run(guard, 0.30f, 3000);
	Check(std::abs(guard.AgcGain() * 0.30f - 0.10f) < 0.001f, "Loud song did not reach target");
	Run(guard, 0.06f, 3000);
	Check(std::abs(guard.AgcGain() * 0.06f - 0.10f) < 0.001f, "Quiet song did not reach target");
	const float beforeSilence = guard.AgcGain();
	Run(guard, 0.0f, 3000);
	Check(std::abs(guard.AgcGain() - beforeSilence) < 1e-6f, "Silence raised the gain before the next song");
	Run(guard, 0.006f, 3000);
	Check(guard.AgcGain() <= std::pow(10.0f, 6.0f / 20.0f) + 1e-6f, "Boost limit exceeded");
	Run(guard, 1.0f, 3000);
	Check(guard.AgcGain() >= std::pow(10.0f, -18.0f / 20.0f) - 1e-6f, "Cut limit exceeded");
	guard.SetAgcLive(false, 0.10f);
	float bypass[] = { 0.8f, -0.2f, 0.0f };
	guard.ProcessChannel(0, bypass, 3);
	Check(bypass[0] == 0.8f && bypass[1] == -0.2f && bypass[2] == 0.0f, "Disabled leveling changed samples");
	guard.SetAgcLive(true, 0.05f);
	Run(guard, 0.10f, 3000);
	Check(std::abs(guard.AgcGain() * 0.10f - 0.05f) < 0.001f, "Live target change failed");
	guard.SetLimiterLive(true, 0.20f);
	float peaks[480];
	std::fill_n(peaks, 480, 1.0f);
	guard.ProcessChannel(0, peaks, 480);
	for (float sample : peaks) Check(std::abs(sample) <= 0.20f, "Limiter ceiling exceeded with leveling enabled");
	std::cout << "Output leveling tests passed\n";
}
