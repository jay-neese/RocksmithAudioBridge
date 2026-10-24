#pragma once
#include <algorithm>
#include <cstdint>
#include <locale>
#include <sstream>

namespace Audio::SongVolume
{
	inline int DefaultLufsTarget(int rmsTarget) { return std::clamp(rmsTarget - 90, -360, -140); }

	struct Configuration
	{
		bool enabled = false;
		int rmsTarget = -200, boost = 0, mode = 0, lufsTarget = -290;
		bool compressPeaks = false;

		// One atomic word makes a live mode/target change indivisible to audio.
		uint32_t Pack() const
		{
			return (enabled ? 1u : 0u) | (mode == 1 ? 2u : 0u)
				| (static_cast<uint32_t>(std::clamp(rmsTarget, -300, -120) + 300) << 2)
				| (static_cast<uint32_t>(std::clamp(lufsTarget, -360, -40) + 360) << 10)
				| (static_cast<uint32_t>(std::clamp(boost, 0, 100)) << 19)
				| (compressPeaks ? 1u << 26 : 0u);
		}
		static Configuration Unpack(uint32_t value)
		{
			return { (value & 1) != 0, static_cast<int>((value >> 2) & 255) - 300,
				static_cast<int>((value >> 19) & 127), (value & 2) ? 1 : 0, static_cast<int>((value >> 10) & 511) - 360,
				(value & (1u << 26)) != 0 };
		}
	};

	// Collapse the former mode + boost controls into one LUFS goal. Folding old
	// boost into the target preserves its intent without keeping hidden extra gain.
	inline Configuration SimplifyConfiguration(Configuration previous)
	{
		previous.rmsTarget = std::clamp(previous.rmsTarget, -300, -120);
		previous.lufsTarget = std::clamp(previous.lufsTarget, -360, -40);
		previous.boost = std::clamp(previous.boost, 0, 100);
		if (previous.mode == 0) previous.lufsTarget = DefaultLufsTarget(previous.rmsTarget);
		previous.lufsTarget = std::clamp(previous.lufsTarget + previous.boost, -360, -40);
		previous.compressPeaks = previous.compressPeaks || previous.boost > 0;
		previous.boost = 0;
		previous.mode = 1;
		return previous;
	}

	// The L-prefixed command is used by the simplified UI. The old numeric
	// two/three/five-field forms remain readable for upgrade compatibility.
	inline bool ParseConfiguration(const wchar_t* text, const Configuration& previous, Configuration& result)
	{
		if (!text) return false;
		std::wistringstream input(text); input.imbue(std::locale::classic());
		// Simplified UI command: L,on,targetLufs,allowCompression.
		if (input.peek() == L'L')
		{
			input.get();
			int on = 0, target = 0, compression = 0;
			if (input.get() != L',' || !(input >> on) || input.get() != L',' || !(input >> target)
				|| input.get() != L',' || !(input >> compression) || input.peek() != std::char_traits<wchar_t>::eof()
				|| (on != 0 && on != 1) || target < -360 || target > -40 || (compression != 0 && compression != 1)) return false;
			Configuration next = previous;
			next.enabled = on != 0; next.lufsTarget = target; next.compressPeaks = compression != 0;
			next.mode = 1; next.boost = 0;
			result = next;
			return true;
		}
		int values[5]{}; unsigned count = 0;
		for (;;)
		{
			if (count == 5 || !(input >> values[count++])) return false;
			if (input.peek() == std::char_traits<wchar_t>::eof()) break;
			if (input.get() != L',') return false;
		}
		if ((count != 2 && count != 3 && count != 5) || (values[0] != 0 && values[0] != 1)) return false;
		Configuration next = previous;
		next.enabled = values[0] != 0; next.rmsTarget = values[1];
		if (count >= 3) next.boost = values[2];
		if (count == 5) { next.mode = values[3]; next.lufsTarget = values[4]; }
		if (next.rmsTarget < -300 || next.rmsTarget > -120 || next.boost < 0 || next.boost > 100
			|| next.mode < 0 || next.mode > 1 || next.lufsTarget < -360 || next.lufsTarget > (count == 5 ? -140 : -40)) return false;
		result = next;
		return true;
	}
}
