#pragma once
#include <cstdint>
#include <string>
#include "SongVolumeDsp.hpp"

namespace Audio::SongVolume
{
	void Configure(bool enabled, int targetTenths, int boostTenths);
	bool Enabled();
	int TargetTenths();
	int BoostTenths();
	std::string Status();
	void Poll(); // game control thread only; process/files never touched from the audio callback
	void Process(int16_t* samples, unsigned frames, uint64_t totalFrames, unsigned sampleRate, ProcessingState& state);
}
