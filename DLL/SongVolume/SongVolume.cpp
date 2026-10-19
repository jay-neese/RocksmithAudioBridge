#include "stdafx.h"
#include "SongVolume.hpp"
#include "SongVolumeDsp.hpp"
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
		std::atomic<bool> enabled{ false }, playing{ false };
		std::atomic<bool> matchedPlayback{ false };
		std::atomic<int> target{ -200 };
		std::atomic<int> boost{ 0 };
		std::atomic<float> peakReductionDb{ 0 };
		// Seqlock snapshot: audio never waits or takes a mutex if publication is in progress.
		std::atomic<unsigned> revision{ 0 }, rate{ 0 };
		std::atomic<uint64_t> length{ 0 };
		std::atomic<float> songRms{ 0 }, songPeak{ 0 }, songBankDb{ ReferenceBankDb };
		std::filesystem::path gameDirectory, scratch, resultPath, errorPath;
		std::string selectedSong;
		std::mutex statusMutex;
		std::string status = "Select a song to measure its backing track.";
		HANDLE helper = nullptr, helperJob = nullptr;
		ULONGLONG helperStarted = 0;
		unsigned requestNumber = 0;
		bool wasEnabled = false;
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
				enabled.store(GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeNormalize", 0, ini.c_str()) != 0);
				target.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeTarget", -200, ini.c_str())), -300, -120));
				boost.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Mod Settings", L"SongVolumeBoost", 0, ini.c_str())), 0, 100));
			});
		}

		void Publish(unsigned sampleRate = 0, uint64_t frames = 0, float rms = 0, float peak = 0, float bankDb = ReferenceBankDb)
		{
			revision.fetch_add(1);
			rate.store(sampleRate); length.store(frames); songRms.store(rms); songPeak.store(peak);
			songBankDb.store(bankDb);
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
			SetStatus("Analyzing backing track; original song level until ready...");
		}
	}

	void Configure(bool on, int tenths, int boostTenths)
	{
		Initialize(); target.store(std::clamp(tenths, -300, -120)); boost.store(std::clamp(boostTenths, 0, 100)); enabled.store(on);
	}
	bool Enabled() { Initialize(); return enabled.load(); }
	int TargetTenths() { Initialize(); return target.load(); }
	int BoostTenths() { Initialize(); return boost.load(); }
	std::string Status()
	{
		if (enabled.load() && rate.load() != 0)
		{
			if (!matchedPlayback.load()) return "Analysis ready; waiting for matching backing-track playback.";
			const float rms = songRms.load(), peak = songPeak.load(), bankDb = songBankDb.load();
			const int boostValue = boost.load();
			const float gain = CalculateGain(rms, peak, target.load(), boostValue == 0, bankDb);
			std::ostringstream text; text.imbue(std::locale::classic()); text.setf(std::ios::fixed); text.precision(1);
			text << "Backing-track correction: " << (gain >= 1 ? "+" : "") << 20.0f * std::log10(gain) << " dB. ";
			text << "Soundbank volume: " << bankDb << " dB (accounted for). ";
			const float desired = std::pow(10.0f, target.load() / 200.0f);
			if (boostValue > 0)
				text << "Song boost: +" << boostValue / 10.0f << " dB. Song limiter reduction: " << peakReductionDb.load() << " dB.";
			else text << (rms * gain * std::pow(10.0f, (bankDb - ReferenceBankDb) / 20.0f) < desired * 0.99f
				? "Target limited by peak protection or gain bounds." : "Song dynamics and guitar level unchanged.");
			return text.str();
		}
		std::lock_guard<std::mutex> lock(statusMutex);
		return status;
	}

	void Poll()
	{
		Initialize();
		CleanTemporaryDirectories();
		playing.store(GameState::IsInSong());
		const bool on = enabled.load();
		const auto key = on ? GameState::GetSongKey() : std::string();
		if (key != selectedSong || on != wasEnabled)
		{
			StopHelper(); Publish(); selectedSong = key; wasEnabled = on;
			SetStatus(on ? "Select a song to measure its backing track." : "Off. Song audio is unchanged.");
			if (!key.empty())
			{
				try { StartHelper(key); }
				catch (const std::exception& error) { StopHelper(); SetStatus(std::string("Original song level: ") + error.what()); }
			}
		}
		if (!helper) return;
		const DWORD wait = WaitForSingleObject(helper, 0);
		if (wait == WAIT_TIMEOUT && GetTickCount64() - helperStarted < 120000) return;
		DWORD code = 1;
		if (wait == WAIT_OBJECT_0) GetExitCodeProcess(helper, &code);
		unsigned version = 0, sampleRate = 0; uint64_t frames = 0; float rms = 0, peak = 0, bankDb = 0;
		std::ifstream response(resultPath); response.imbue(std::locale::classic());
		if (code == 0 && (response >> version >> sampleRate >> frames >> rms >> peak >> bankDb) && version == 2
			&& (sampleRate == 44100 || sampleRate == 48000) && frames >= sampleRate * 20ULL
			&& std::isfinite(rms) && std::isfinite(peak) && rms > 0 && peak >= rms && peak <= 1
			&& ValidBankVolume(bankDb))
		{
			Publish(sampleRate, frames, rms, peak, bankDb);
			SetStatus("Ready. One fixed correction per song; your guitar level is unchanged.");
			LOG_INFO("(SONG VOLUME) Measured " << selectedSong << ": RMS=" << rms << " peak=" << peak << " bankDb=" << bankDb << std::endl);
		}
		else
		{
			SetStatus("Analysis unavailable; original song level. Disable and enable to retry. See RSMods_debug.txt.");
			std::ifstream errors(errorPath);
			std::string detail((std::istreambuf_iterator<char>(errors)), std::istreambuf_iterator<char>());
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
		const unsigned before = revision.load();
		const auto expectedFrames = length.load(); const auto expectedRate = rate.load();
		const float rms = songRms.load(), peak = songPeak.load(), bankDb = songBankDb.load();
		if (!(before & 1) && before == revision.load() && enabled.load() && playing.load()
			&& MatchesSong(expectedRate, expectedFrames, sampleRate, totalFrames))
		{
			const int boostValue = boost.load();
			// Positive bank gain needs live protection during the gain ramp as well
			// as a lower static peak ceiling once the correction has settled.
			limitPeaks = boostValue > 0 || bankDb > 0;
			activeBankDb = bankDb;
			wanted = CalculateGain(rms, peak, target.load(), boostValue == 0, bankDb) * BoostGain(boostValue);
			matchedPlayback.store(true);
		}
		ApplySongGain(samples, frames, sampleRate, wanted, limitPeaks, state, activeBankDb);
		if (limitPeaks) peakReductionDb.store(static_cast<float>(-20.0 * std::log10(std::max(0.000001, state.limiterGain))));
	}
}
