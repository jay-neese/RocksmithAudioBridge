#pragma once
#include "SongVolumeDsp.hpp"
#include <istream>
#include <string>

namespace Audio::SongVolume
{
	struct Measurement
	{
		unsigned rate = 0;
		uint64_t frames = 0;
		float rms = 0, peak = 0, bankDb = ReferenceBankDb, lufs = 0;
	};

	inline bool ReadMeasurement(std::istream& input, Measurement& result)
	{
		Measurement next; unsigned version = 0; std::string trailing;
		if (!(input >> version >> next.rate >> next.frames >> next.rms >> next.peak >> next.bankDb >> next.lufs)
			|| version != 3 || (next.rate != 44100 && next.rate != 48000) || next.frames < next.rate * 20ULL || next.frames > UINT32_MAX / 4ULL
			|| !std::isfinite(next.rms) || !std::isfinite(next.peak) || next.rms <= 0 || next.peak < next.rms || next.peak > 1
			|| !ValidBankVolume(next.bankDb) || !ValidLoudness(next.lufs) || (input >> trailing)) return false;
		result = next;
		return true;
	}
}
