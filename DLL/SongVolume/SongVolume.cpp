#include "stdafx.h"
#include "SongVolume.hpp"
#include "SongVolumeDsp.hpp"
#include "SongVolumeMeasurement.hpp"
#include "GameState.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <locale>

namespace Audio::SongVolume
{
	namespace
	{
		std::once_flag initialized;
		std::atomic<bool> playing{ false };
		std::atomic<bool> matchedPlayback{ false };
		std::atomic<uint32_t> configuration{ Configuration{}.Pack() };
		std::atomic<float> peakReductionDb{ 0 };
		// Seqlock snapshot: audio never waits or takes a mutex if publication is in progress.
		std::atomic<unsigned> revision{ 0 }, rate{ 0 };
		std::atomic<uint64_t> length{ 0 };
		std::atomic<float> songRms{ 0 }, songPeak{ 0 }, songBankDb{ ReferenceBankDb }, songLufs{ 0 };
		Configuration ReadConfiguration() { return Configuration::Unpack(configuration.load()); }
		std::filesystem::path gameDirectory, scratch, resultPath, errorPath;
		std::string selectedSong;
		std::mutex statusMutex;
		std::string status = "Choose a song to prepare automatic volume.";
		HANDLE helper = nullptr, helperJob = nullptr;
		ULONGLONG helperStarted = 0;
		unsigned requestNumber = 0;
		bool wasEnabled = false;
		uint32_t reportedConfiguration = UINT32_MAX;
		unsigned reportedRevision = UINT32_MAX;
		ULONGLONG reportedAt = 0;
		std::vector<std::filesystem::path> cleanupDirectories;

		void CleanTemporaryDirectories()
		{
			for (auto it = cleanupDirectories.begin(); it != cleanupDirectories.end(); )
			{
				std::error_code error;
				// Only our per-request child directory may be removed, never the temp root.
				if (it->parent_path() != scratch) { it = cleanupDirectories.erase(it); continue; }
				std::filesystem::remove_all(*it, error);
				if (!error) it = cleanupDirectories.erase(it); else ++it;
			}
		}

		void SetStatus(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(statusMutex);
			status = text;
		}

		void Initialize()
		{
			std::call_once(initialized, [] {
				wchar_t executable[MAX_PATH]{};
				GetModuleFileNameW(nullptr, executable, MAX_PATH);
				gameDirectory = std::filesystem::path(executable).parent_path();
				const auto ini = gameDirectory / L"RSMods.ini";
				wchar_t previousRms[32]{};
				const bool hadRmsSetting = GetPrivateProfileStringW(L"Mod Settings", L"SongVolumeTarget", L"", previousRms, 32, ini.c_str()) != 0;
				Configuration settings;
				settings.enabled = GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeNormalize", 0, ini.c_str()) != 0;
				settings.rmsTarget = static_cast<int>(GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeTarget", -200, ini.c_str()));
				settings.boost = static_cast<int>(GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeBoost", 0, ini.c_str()));
				settings.mode = GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeMode", hadRmsSetting ? 0 : 1, ini.c_str()) == 1 ? 1 : 0;
				settings.lufsTarget = static_cast<int>(GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeLufsTarget",
					hadRmsSetting ? DefaultLufsTarget(std::clamp(settings.rmsTarget, -300, -120)) : -210, ini.c_str()));
				settings.compressPeaks = GetPrivateProfileIntW(L"Mod Settings", L"SongVolumePeakCompression", settings.boost > 0 ? 1 : 0, ini.c_str()) != 0;
				configuration.store(SimplifyConfiguration(settings).Pack());
			});
		}

		void Publish(unsigned sampleRate = 0, uint64_t frames = 0, float rms = 0, float peak = 0, float bankDb = ReferenceBankDb, float lufs = 0)
		{
			revision.fetch_add(1);
			rate.store(sampleRate); length.store(frames); songRms.store(rms); songPeak.store(peak);
			songBankDb.store(bankDb);
			songLufs.store(lufs);
			matchedPlayback.store(false);
			peakReductionDb.store(0);
			revision.fetch_add(1);
		}

		void StopHelper()
		{
			if (helper)
			{
				// This process belongs only to the current analysis request.
				if (WaitForSingleObject(helper, 0) == WAIT_TIMEOUT) TerminateProcess(helper, ERROR_CANCELLED);
				CloseHandle(helper); helper = nullptr;
			}
			if (helperJob) { CloseHandle(helperJob); helperJob = nullptr; }
			if (!resultPath.empty())
			{
				cleanupDirectories.push_back(resultPath.parent_path());
				resultPath.clear(); errorPath.clear();
			}
			CleanTemporaryDirectories();
		}

		std::wstring Quote(const std::wstring& value)
		{
			std::wstring out = L"\"";
			unsigned slashes = 0;
			for (wchar_t c : value)
			{
				if (c == L'\\') { ++slashes; continue; }
				out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
				out += c; slashes = 0;
			}
			out.append(slashes * 2, L'\\');
			return out + L'"';
		}

		void StartHelper(const std::string& key)
		{
			wchar_t temporary[MAX_PATH]{};
			if (!GetTempPathW(MAX_PATH, temporary)) throw std::runtime_error("No temporary directory.");
			scratch = std::filesystem::path(temporary) / L"RocksmithSongVolume";
			std::filesystem::create_directories(scratch);
			const auto requestDirectory = scratch / (std::to_wstring(GetCurrentProcessId()) + L"-"
				+ std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++requestNumber));
			std::filesystem::create_directories(requestDirectory);
			resultPath = requestDirectory / L"measurement.txt";
			errorPath = resultPath.wstring() + L".error";
			const auto executable = gameDirectory / L"RSMods" / L"RSMods.exe";
			const std::string bank = "Song_" + key + ".bnk";
			const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bank.data(), static_cast<int>(bank.size()), nullptr, 0);
			if (!count) throw std::runtime_error("Invalid song key.");
			std::wstring wideBank(count, 0);
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bank.data(), static_cast<int>(bank.size()), wideBank.data(), count);
			std::wstring command = Quote(executable.wstring()) + L" --song-volume-analyze " + Quote(gameDirectory.wstring())
				+ L" " + Quote(wideBank) + L" " + Quote(resultPath.wstring()) + L" " + Quote(errorPath.wstring());
			helperJob = CreateJobObjectW(nullptr, nullptr);
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (!helperJob || !SetInformationJobObject(helperJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
				throw std::runtime_error("Could not create the analysis process job.");
			STARTUPINFOW startup{}; startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
				nullptr, gameDirectory.c_str(), &startup, &process)) throw std::runtime_error("Could not launch the song analyzer. Reinstall the matching RSMods.exe.");
			helper = process.hProcess;
			const bool assigned = AssignProcessToJobObject(helperJob, helper) != FALSE;
			const bool resumed = assigned && ResumeThread(process.hThread) != static_cast<DWORD>(-1);
			CloseHandle(process.hThread);
			if (!resumed) throw std::runtime_error("Could not attach/start the analyzer for the game lifetime.");
			helperStarted = GetTickCount64();
			SetStatus("Measuring this song. Playing its original volume until ready...");
		}
	}

	void Configure(const Configuration& settings)
	{
		Initialize(); configuration.store(SimplifyConfiguration(settings).Pack());
	}
	bool Enabled() { Initialize(); return ReadConfiguration().enabled; }
	int TargetTenths() { Initialize(); return ReadConfiguration().rmsTarget; }
	int BoostTenths() { Initialize(); return ReadConfiguration().boost; }
	int Mode() { Initialize(); return ReadConfiguration().mode; }
	int LufsTargetTenths() { Initialize(); return ReadConfiguration().lufsTarget; }
	bool PeakCompression() { Initialize(); return ReadConfiguration().compressPeaks; }
	LoudnessRange AvailableLoudnessRange(bool allowCompression)
	{
		const unsigned before = revision.load();
		const auto sampleRate = rate.load();
		const float lufs = songLufs.load(), peak = songPeak.load(), bankDb = songBankDb.load();
		if ((before & 1) || before != revision.load() || sampleRate == 0) return {};
		return CalculateLoudnessRange(lufs, peak, bankDb, allowCompression);
	}
	Configuration CurrentConfiguration() { Initialize(); return ReadConfiguration(); }
	std::string Status()
	{
		const auto settings = ReadConfiguration();
		if (!settings.enabled) return "Off. Songs play at their original volume.";
		if (settings.enabled && rate.load() != 0)
		{
			if (!matchedPlayback.load()) return "Ready. Play or resume this song to apply automatic volume.";
			const float rms = songRms.load(), peak = songPeak.load(), bankDb = songBankDb.load();
			const float lufs = songLufs.load();
			const float gain = NormalizedGain(settings, rms, peak, bankDb, lufs);
			const float unrestricted = CalculateLufsGain(lufs, peak, settings.lufsTarget, false, bankDb, 22.0f);
			if (!settings.compressPeaks && unrestricted > gain * 1.01f)
				return "This song is at its peak limit. Turn on Allow peak compression to make it louder.";
			const float difference = lufs + bankDb + 20.0f * std::log10(gain) - settings.lufsTarget / 10.0f;
			if (std::abs(difference) > 0.1f)
				return "At the adjustment limit. This song may not fully match your chosen loudness.";
			if (settings.compressPeaks && peakReductionDb.load() > 0.1f)
				return "Reducing loud peaks to help this song reach your chosen loudness. Your guitar is unchanged.";
			return "Leveling active. Your guitar volume is unchanged.";
		}
		std::lock_guard<std::mutex> lock(statusMutex);
		return status;
	}

	void Poll()
	{
		Initialize();
		CleanTemporaryDirectories();
		playing.store(GameState::IsInSong());
		if (!playing.load()) { matchedPlayback.store(false); reportedRevision = UINT32_MAX; }
		const bool on = ReadConfiguration().enabled;
		const auto key = on ? GameState::GetSongKey() : std::string();
		if (key != selectedSong || on != wasEnabled)
		{
			StopHelper(); Publish(); selectedSong = key; wasEnabled = on;
			SetStatus(on ? "Choose a song to prepare automatic volume." : "Off. Songs play at their original volume.");
			if (!key.empty())
			{
				try { StartHelper(key); }
				catch (const std::exception& error)
				{
					StopHelper(); SetStatus("Couldn't prepare song leveling. Playing at the original volume.");
					LOG_ERROR("(SONG VOLUME) Could not start analysis for " << key << ": " << error.what() << std::endl);
				}
			}
		}
		const auto settings = ReadConfiguration();
		const unsigned currentRevision = revision.load();
		const ULONGLONG now = GetTickCount64();
		if (settings.enabled && playing.load() && matchedPlayback.load() && rate.load() != 0
			&& (reportedRevision != currentRevision || reportedConfiguration != settings.Pack()
				|| (settings.compressPeaks && now - reportedAt >= 5000)))
		{
			reportedRevision = currentRevision; reportedConfiguration = settings.Pack();
			reportedAt = now;
			const float gain = NormalizedGain(settings, songRms.load(), songPeak.load(), songBankDb.load(), songLufs.load());
			LOG_INFO("(SONG VOLUME) Matched " << selectedSong << ": mode=" << (settings.mode == 1 ? "LUFS" : "RMS")
				<< " target=" << (settings.mode == 1 ? settings.lufsTarget : settings.rmsTarget) / 10.0f
				<< " requestedGainDb=" << 20.0f * std::log10(gain) << " compressionAllowed=" << (settings.compressPeaks ? 1 : 0)
				<< " peakReductionDb=" << peakReductionDb.load()
				<< " bankDb=" << songBankDb.load() << " sourceLUFS=" << songLufs.load() << std::endl);
		}
		if (!helper) return;
		const DWORD wait = WaitForSingleObject(helper, 0);
		if (wait == WAIT_TIMEOUT && GetTickCount64() - helperStarted < 120000) return;
		DWORD code = 1;
		if (wait == WAIT_OBJECT_0) GetExitCodeProcess(helper, &code);
		Measurement measurement;
		std::ifstream response(resultPath); response.imbue(std::locale::classic());
		if (code == 0 && ReadMeasurement(response, measurement))
		{
			Publish(measurement.rate, measurement.frames, measurement.rms, measurement.peak, measurement.bankDb, measurement.lufs);
			SetStatus("Ready. Play or resume this song to apply automatic volume.");
			LOG_INFO("(SONG VOLUME) Measured " << selectedSong << ": RMS=" << measurement.rms << " peak=" << measurement.peak
				<< " bankDb=" << measurement.bankDb << " LUFS=" << measurement.lufs << std::endl);
		}
		else
		{
			SetStatus(code == 0 ? "Song leveling is unavailable. Reinstall the current Audio Bridge build."
				: "Couldn't measure this song. Playing it at its original volume.");
			std::ifstream errors(errorPath);
			std::string detail((std::istreambuf_iterator<char>(errors)), std::istreambuf_iterator<char>());
			if (code == 0) detail = "Invalid measurement response; expected version 3 RMS and LUFS data.";
			LOG_ERROR("(SONG VOLUME) Analysis failed for " << selectedSong << ": " << (wait == WAIT_TIMEOUT ? "timed out" : detail) << std::endl);
		}
		response.close();
		StopHelper();
	}

	void Process(int16_t* samples, unsigned frames, uint64_t totalFrames, unsigned sampleRate, ProcessingState& state)
	{
		float wanted = 1;
		float activeBankDb = ReferenceBankDb;
		bool limitPeaks = false;
		const auto settings = ReadConfiguration();
		const unsigned before = revision.load();
		const auto expectedFrames = length.load(); const auto expectedRate = rate.load();
		const float rms = songRms.load(), peak = songPeak.load(), bankDb = songBankDb.load(), lufs = songLufs.load();
		if (!(before & 1) && before == revision.load() && settings.enabled && playing.load()
			&& MatchesSong(expectedRate, expectedFrames, sampleRate, totalFrames))
		{
			const int boostValue = settings.boost;
			// Positive bank gain needs live protection during the gain ramp as well
			// as a lower static peak ceiling once the correction has settled.
			limitPeaks = settings.compressPeaks || boostValue > 0 || bankDb > 0;
			activeBankDb = bankDb;
			const float gain = NormalizedGain(settings, rms, peak, bankDb, lufs);
			wanted = gain * BoostGain(boostValue);
			matchedPlayback.store(true);
		}
		ApplySongGain(samples, frames, sampleRate, wanted, limitPeaks, state, activeBankDb);
		peakReductionDb.store(static_cast<float>(-20.0 * std::log10(std::max(0.000001, state.limiterGain))));
	}
}
