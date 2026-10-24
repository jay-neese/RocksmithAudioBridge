using System;
using System.Globalization;

namespace RSMods.Audio
{
	internal sealed class SongVolumeMeasurement
	{
		public uint SampleRate;
		public ulong Frames;
		public double Rms, Peak, BankDb, IntegratedLufs;

		public static bool TryParse(string text, out SongVolumeMeasurement result)
		{
			result = null;
			if (text == null) return false;
			var fields = text.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
			var ci = CultureInfo.InvariantCulture;
			if (fields.Length != 7 || fields[0] != "3"
				|| !uint.TryParse(fields[1], NumberStyles.Integer, ci, out uint rate) || (rate != 44100 && rate != 48000)
				|| !ulong.TryParse(fields[2], NumberStyles.Integer, ci, out ulong frames) || frames < rate * 20UL || frames > uint.MaxValue / 4UL
				|| !double.TryParse(fields[3], NumberStyles.Float, ci, out double rms) || !(rms > 0)
				|| !double.TryParse(fields[4], NumberStyles.Float, ci, out double peak) || !(peak >= rms && peak <= 1)
				|| !double.TryParse(fields[5], NumberStyles.Float, ci, out double bank) || !(bank >= -96 && bank <= 24)
				|| !double.TryParse(fields[6], NumberStyles.Float, ci, out double lufs) || !(lufs >= -70 && lufs <= 12)) return false;
			result = new SongVolumeMeasurement { SampleRate = rate, Frames = frames, Rms = rms, Peak = peak, BankDb = bank, IntegratedLufs = lufs };
			return true;
		}

		public string Serialize()
		{
			return string.Format(CultureInfo.InvariantCulture, "3 {0} {1} {2:R} {3:R} {4:R} {5:R}\n",
				SampleRate, Frames, Rms, Peak, BankDb, IntegratedLufs);
		}
	}
}
