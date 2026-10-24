using System;
using System.Collections.Generic;
using System.IO;

namespace RSMods.Audio
{
	// Audit only: BS.1770 K weighting and 400 ms gates with 75% overlap.
	// Coefficient construction follows libebur128's ebur128_init_filter:
	// https://github.com/jiixyj/libebur128/blob/master/ebur128/ebur128.c
	// Sample peaks, not oversampled true peaks, are measured by the production analyzer.
	internal sealed class SongLoudnessAudit
	{
		public double IntegratedLufs, ShortTermP10, ShortTermP95;

		private struct Filter
		{
			public double B0, B1, B2, A1, A2, Z1, Z2;
			public double Run(double x)
			{
				double y = B0 * x + Z1;
				Z1 = B1 * x - A1 * y + Z2;
				Z2 = B2 * x - A2 * y;
				return y;
			}
		}
		private static Filter Shelf(int rate)
		{
			double k = Math.Tan(Math.PI * 1681.974450955533 / rate);
			double vh = Math.Pow(10, 3.999843853973347 / 20), vb = Math.Pow(vh, 0.4996667741545416);
			double q = 0.7071752369554196, d = 1 + k / q + k * k;
			return new Filter { B0 = (vh + vb * k / q + k * k) / d, B1 = 2 * (k * k - vh) / d,
				B2 = (vh - vb * k / q + k * k) / d, A1 = 2 * (k * k - 1) / d, A2 = (1 - k / q + k * k) / d };
		}
		private static Filter HighPass(int rate)
		{
			double k = Math.Tan(Math.PI * 38.13547087602444 / rate), q = 0.5003270373238773;
			double d = 1 + k / q + k * k;
			return new Filter { B0 = 1, B1 = -2, B2 = 1, A1 = 2 * (k * k - 1) / d, A2 = (1 - k / q + k * k) / d };
		}
		private static double Lufs(double energy) { return -0.691 + 10 * Math.Log10(energy); }
		private static double Percentile(List<double> values, double p)
		{
			if (values.Count == 0) return double.NaN;
			values.Sort();
			return values[(int)Math.Round((values.Count - 1) * p)];
		}
		public static SongLoudnessAudit ReadWave(string path)
		{
			using (var stream = File.OpenRead(path))
			using (var reader = new BinaryReader(stream))
			{
				if (reader.ReadUInt32() != 0x46464952) throw new InvalidDataException("Expected RIFF.");
				reader.ReadUInt32();
				if (reader.ReadUInt32() != 0x45564157) throw new InvalidDataException("Expected WAVE.");
				int rate = 0; long dataOffset = 0; uint dataBytes = 0;
				while (stream.Position + 8 <= stream.Length)
				{
					uint id = reader.ReadUInt32(), size = reader.ReadUInt32(); long start = stream.Position;
					if (size > stream.Length - start) throw new InvalidDataException("Truncated WAV.");
					if (id == 0x20746d66)
					{
						if (size < 16 || reader.ReadUInt16() != 1 || reader.ReadUInt16() != 2) throw new InvalidDataException("Expected stereo PCM.");
						rate = reader.ReadInt32(); reader.ReadUInt32();
						if (reader.ReadUInt16() != 4 || reader.ReadUInt16() != 16 || (rate != 44100 && rate != 48000))
							throw new InvalidDataException("Expected 44.1/48 kHz PCM16.");
					}
					if (id == 0x61746164) { dataOffset = start; dataBytes = size; }
					stream.Position = start + size + (size & 1);
				}
				if (rate == 0 || dataOffset == 0 || dataBytes == 0 || dataBytes % 4 != 0) throw new InvalidDataException("Missing WAV data.");
				stream.Position = dataOffset;
				var ls = Shelf(rate); var rs = Shelf(rate); var lh = HighPass(rate); var rh = HighPass(rate);
				var blocks = new List<double>(); var shortTerms = new List<double>();
				var recent = new double[30]; int hop = rate / 10, count = 0, segments = 0;
				double sum = 0;
				// Read blocks rather than calling BinaryReader once per sample.
				for (long remaining = dataBytes; remaining > 0; )
				{
					byte[] bytes = reader.ReadBytes((int)Math.Min(65536, remaining));
					if (bytes.Length == 0 || bytes.Length % 4 != 0) throw new InvalidDataException("Truncated audio.");
					remaining -= bytes.Length;
					for (int i = 0; i < bytes.Length; i += 4)
					{
						double l = lh.Run(ls.Run((short)(bytes[i] | bytes[i + 1] << 8) / 32768.0));
						double r = rh.Run(rs.Run((short)(bytes[i + 2] | bytes[i + 3] << 8) / 32768.0));
						sum += l * l + r * r;
						if (++count != hop) continue;
						recent[segments % 30] = sum / hop; ++segments; count = 0; sum = 0;
						if (segments >= 4)
						{
							double block = 0;
							for (int j = 0; j < 4; ++j) block += recent[(segments - 1 - j) % 30];
							blocks.Add(block / 4);
						}
						if (segments >= 30)
						{
							double energy = 0; foreach (double x in recent) energy += x;
							if (Lufs(energy / 30) > -70) shortTerms.Add(Lufs(energy / 30));
						}
					}
				}
				double floor = Math.Pow(10, (-70 + 0.691) / 10), energySum = 0; int accepted = 0;
				foreach (double x in blocks) if (x > floor) { energySum += x; ++accepted; }
				if (accepted == 0) throw new InvalidDataException("No measurable loudness.");
				double gate = Math.Max(floor, energySum / accepted * 0.1);
				energySum = 0; accepted = 0;
				foreach (double x in blocks) if (x >= gate) { energySum += x; ++accepted; }
				return new SongLoudnessAudit { IntegratedLufs = Lufs(energySum / accepted),
					ShortTermP10 = Percentile(shortTerms, 0.10), ShortTermP95 = Percentile(shortTerms, 0.95) };
			}
		}
	}
}
