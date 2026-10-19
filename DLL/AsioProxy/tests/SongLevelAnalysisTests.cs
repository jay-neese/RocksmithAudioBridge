using System;
using System.IO;
using System.Text;
using RSMods.Audio;

class SongLevelAnalysisTests
{
	static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
	static MemoryStream Wave(double amplitude, int seconds = 20, int silentSeconds = 0, bool quietSection = false)
	{
		const uint rate = 48000;
		int frames = (seconds + silentSeconds) * (int)rate;
		var stream = new MemoryStream();
		using (var writer = new BinaryWriter(stream, Encoding.ASCII, true))
		{
			writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write((uint)(36 + frames * 4));
			writer.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16u);
			writer.Write((ushort)1); writer.Write((ushort)2); writer.Write(rate); writer.Write(rate * 4);
			writer.Write((ushort)4); writer.Write((ushort)16);
			writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write((uint)(frames * 4));
			for (int i = 0; i < frames; ++i)
			{
				double a = i < silentSeconds * rate ? 0 : amplitude;
				if (quietSection && i > frames / 2) a *= 0.1;
				short sample = (short)Math.Round(a * 32767 * Math.Sin(2 * Math.PI * 1000 * i / rate));
				writer.Write(sample); writer.Write(sample);
			}
		}
		stream.Position = 0;
		return stream;
	}
	static SongLevelAnalysis Analyze(double amplitude, int silence = 0, bool quiet = false)
	{
		using (var stream = Wave(amplitude, 20, silence, quiet)) return SongLevelAnalysis.ReadWave(stream);
	}
	static void Main()
	{
		var loud = Analyze(0.6); var quiet = Analyze(0.15); var silence = Analyze(0.6, 4);
		Check(Math.Abs(loud.Rms / quiet.Rms - 4) < 0.01, "Track loudness ratio incorrect");
		Check(Math.Abs(loud.Rms - 0.6 / Math.Sqrt(2)) < 0.001, "RMS scale incorrect");
		Check(Math.Abs(loud.Peak - 0.6) < 0.001, "Peak incorrect");
		Check(Math.Abs(silence.Rms - loud.Rms) < 0.001, "Silent intro changed the target measurement");
		Check(Math.Abs(Analyze(0.6, 0, true).Rms - loud.Rms) < 0.01, "Quiet passage biased the measurement");
		Check(loud.SampleRate == 48000 && loud.Frames == 20 * 48000UL, "Playback identity metadata incorrect");
		bool rejected = false;
		try { Analyze(0); } catch (InvalidDataException) { rejected = true; }
		Check(rejected, "Silent file should bypass analysis");
		using (var stream = Wave(0.5))
		{
			stream.SetLength(stream.Length - 2); rejected = false;
			try { SongLevelAnalysis.ReadWave(stream); } catch (InvalidDataException) { rejected = true; }
			Check(rejected, "Truncated WAV was accepted");
		}
		Console.WriteLine("Offline song measurement tests passed");
	}
}
