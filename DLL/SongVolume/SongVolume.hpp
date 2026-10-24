#pragma once
#include <cstdint>
#include <string>
#include "SongVolumeDsp.hpp"
#include "SongVolumeConfig.hpp"

namespace Audio::SongVolume
{
	inline float NormalizedGain(const Configuration& settings, float rms, float peak, float bankDb, float lufs)
	{
		const bool preservePeaks = !settings.compressPeaks && settings.boost == 0;
		return settings.mode == 1 ? CalculateLufsGain(lufs, peak, settings.lufsTarget, preservePeaks, bankDb, settings.boost > 0 ? 12.0f : 22.0f)
			: CalculateGain(rms, peak, settings.rmsTarget, preservePeaks, bankDb);
	}

	void Configure(const Configuration& settings);
	bool Enabled();
	int TargetTenths();
	int BoostTenths();
	int Mode(); // 0 = existing RMS, 1 = integrated LUFS
	int LufsTargetTenths();
	bool PeakCompression();
	LoudnessRange AvailableLoudnessRange(bool allowCompression);
	Configuration CurrentConfiguration();
	std::string Status();
	void Poll(); // game control thread only; process/files never touched from the audio callback
	void Process(int16_t* samples, unsigned frames, uint64_t totalFrames, unsigned sampleRate, ProcessingState& state);
}
