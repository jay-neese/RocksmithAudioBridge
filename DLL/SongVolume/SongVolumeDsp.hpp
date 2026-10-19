#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Audio::SongVolume
{
	inline bool MatchesSong(unsigned expectedRate, uint64_t expectedFrames, unsigned actualRate, uint64_t actualFrames)
	{
		return expectedFrames != 0 && expectedFrames == actualFrames && expectedRate == actualRate;
	}

	// The target uses a common -12 dB soundbank reference. Compensate before Wwise's
	// authored gain, with the decoder's PCM16 peak ceiling still enforced here.
	constexpr float ReferenceBankDb = -12.0f;
	inline bool ValidBankVolume(float bankDb)
	{
		return std::isfinite(bankDb) && bankDb >= -96 && bankDb <= 24;
	}
	// Positive authored gain happens downstream. Reserve that extra headroom
	// before returning PCM16 samples, without spending negative-bank headroom.
	inline float BankHeadroom(float bankDb)
	{
		return std::pow(10.0f, -std::max(0.0f, bankDb) / 20.0f);
	}
	inline float CalculateGain(float rms, float peak, int targetTenths, bool preservePeakHeadroom = true,
		float bankDb = ReferenceBankDb)
	{
		if (!std::isfinite(rms) || !std::isfinite(peak) || rms <= 0 || peak <= 0 || rms > peak || peak > 1
			|| !ValidBankVolume(bankDb)) return 1;
		const float target = std::pow(10.0f, std::clamp(targetTenths, -300, -120) / 200.0f);
		// Up to +12/-24 dB, and never boost the measured sample peak past -1 dBFS.
		const float gain = std::clamp(target / rms * std::pow(10.0f, (ReferenceBankDb - bankDb) / 20.0f), 0.063095734f, 3.981071706f);
		return preservePeakHeadroom ? std::min(gain, 0.891250938f * BankHeadroom(bankDb) / peak) : gain;
	}

	inline float BoostGain(int tenths) { return std::pow(10.0f, std::clamp(tenths, 0, 100) / 200.0f); }

	struct ProcessingState
	{
		float gain = 1.0f;
		double limiterGain = 1.0;
		bool limiting = false;
	};

	inline void ApplyGain(int16_t* samples, unsigned frames, unsigned sampleRate, float wanted, float& gain)
	{
		if (!samples || !frames || !sampleRate) return;
		if (wanted == 1.0f && gain == 1.0f) return; // exact bypass
		// Ramp at a fixed rate only when the selected song/target/toggle changes, not with its dynamics.
		const float step = 1.0f / (sampleRate * 0.05f);
		for (unsigned frame = 0; frame < frames; ++frame)
		{
			gain += std::clamp(wanted - gain, -step, step);
			for (unsigned ch = 0; ch < 2; ++ch)
			{
				const unsigned i = frame * 2 + ch;
				const long value = std::lround(samples[i] * gain);
				samples[i] = static_cast<int16_t>(std::clamp(value, -32768L, 32767L));
			}
		}
	}

	// A stereo-linked, zero-lookahead peak limiter. Immediate attack protects the current sample;
	// 80 ms release avoids hard-clipping every peak. Runs before PCM16 conversion, only on song audio.
	inline void ApplySongGain(int16_t* samples, unsigned frames, unsigned sampleRate, float wanted,
		bool limitPeaks, ProcessingState& state, float bankDb = ReferenceBankDb)
	{
		if (!samples || !frames || !sampleRate) return;
		if (!ValidBankVolume(bankDb)) return;
		if (!limitPeaks && !state.limiting) { ApplyGain(samples, frames, sampleRate, wanted, state.gain); return; }
		if (limitPeaks) state.limiting = true;
		const float step = 1.0f / (sampleRate * 0.05f);
		const float release = 1.0f - std::exp(-1.0f / (sampleRate * 0.08f));
		const long ceilingSamples = static_cast<long>(std::floor(29204.0f * BankHeadroom(bankDb)));
		const float ceiling = static_cast<float>(ceilingSamples); // <= -1 dBFS after positive bank gain too
		for (unsigned frame = 0; frame < frames; ++frame)
		{
			state.gain += std::clamp(wanted - state.gain, -step, step);
			const float left = samples[frame * 2] * state.gain;
			const float right = samples[frame * 2 + 1] * state.gain;
			// Continue protecting peaks while a previously boosted gain ramps down.
			const bool protect = limitPeaks || state.gain != wanted;
			const float peak = std::max(std::abs(left), std::abs(right));
			const float needed = protect && peak > ceiling ? ceiling / peak : 1.0f;
			if (needed < state.limiterGain) state.limiterGain = needed;
			else state.limiterGain += (needed - state.limiterGain) * release;
			if (state.limiterGain > 0.999999f) state.limiterGain = 1.0f;
			const float bound = protect ? ceiling : 32767.0f;
			for (unsigned ch = 0; ch < 2; ++ch)
			{
				const float value = static_cast<float>((ch == 0 ? left : right) * state.limiterGain);
				const long rounded = std::lround(value);
				// Preserve -32768 on exact bypass; a limiter-enabled sample stays under its ceiling.
				samples[frame * 2 + ch] = static_cast<int16_t>(std::clamp(rounded, protect ? -ceilingSamples : -32768L, static_cast<long>(bound)));
			}
			if (!limitPeaks && state.gain == wanted && state.limiterGain == 1.0f) state.limiting = false;
		}
	}
}
