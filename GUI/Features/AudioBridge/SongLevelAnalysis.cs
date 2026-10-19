using System;
using System.Collections.Generic;
using System.IO;

namespace RSMods.Audio
{
	// Offline gated RMS, not LUFS. 400 ms blocks reject silence and blocks more than
	// 10 dB below the first-pass average; one gain is then used for the whole song.
	internal sealed class SongLevelAnalysis
	{
		public uint SampleRate;
		public ulong Frames;
		public double Rms;
		public double Peak;

		public static SongLevelAnalysis ReadWave(Stream stream)
		{
			using (var reader = new BinaryReader(stream, System.Text.Encoding.ASCII, true))
			{
				if (reader.ReadUInt32() != 0x46464952) throw new InvalidDataException("Expected RIFF WAV.");
				reader.ReadUInt32();
				if (reader.ReadUInt32() != 0x45564157) throw new InvalidDataException("Expected WAVE.");
				uint rate = 0, dataBytes = 0;
				long dataOffset = 0;
				bool formatSeen = false;
				while (stream.Position + 8 <= stream.Length)
				{
					uint id = reader.ReadUInt32(), size = reader.ReadUInt32();
					long start = stream.Position;
					if (size > stream.Length - start) throw new InvalidDataException("Truncated WAV chunk.");
					if (id == 0x20746d66)
					{
						if (size < 16 || reader.ReadUInt16() != 1 || reader.ReadUInt16() != 2)
							throw new InvalidDataException("Expected stereo PCM.");
						rate = reader.ReadUInt32();
						reader.ReadUInt32();
						if (reader.ReadUInt16() != 4 || reader.ReadUInt16() != 16 || (rate != 44100 && rate != 48000))
							throw new InvalidDataException("Expected 44.1/48 kHz stereo PCM16.");
						formatSeen = true;
					}
					if (id == 0x61746164) { dataOffset = start; dataBytes = size; }
					stream.Position = start + size + (size & 1);
				}
				if (!formatSeen || dataOffset == 0 || dataBytes == 0 || dataBytes % 4 != 0)
					throw new InvalidDataException("Missing or invalid WAV audio.");
				stream.Position = dataOffset;
				var result = new SongLevelAnalysis { SampleRate = rate, Frames = dataBytes / 4 };
				var blocks = new List<double>();
				int blockFrames = (int)(rate * 4 / 10), count = 0;
				double energy = 0;
				for (ulong frame = 0; frame < result.Frames; ++frame)
				{
					double left = reader.ReadInt16() / 32768.0, right = reader.ReadInt16() / 32768.0;
					result.Peak = Math.Max(result.Peak, Math.Max(Math.Abs(left), Math.Abs(right)));
					energy += (left * left + right * right) * 0.5;
					if (++count == blockFrames) { blocks.Add(energy / count); energy = 0; count = 0; }
				}
				// Ignore a partial tail for gating; the peak above still includes every sample.
				if (blocks.Count == 0 && count > 0) blocks.Add(energy / count);
				double sum = 0; int accepted = 0;
				foreach (double block in blocks) if (block > 1e-7) { sum += block; ++accepted; }
				if (accepted == 0) throw new InvalidDataException("No measurable song audio.");
				double gate = Math.Max(1e-7, sum / accepted * 0.1);
				sum = 0; accepted = 0;
				foreach (double block in blocks) if (block >= gate) { sum += block; ++accepted; }
				result.Rms = Math.Sqrt(sum / accepted);
				return result;
			}
		}
	}
}
