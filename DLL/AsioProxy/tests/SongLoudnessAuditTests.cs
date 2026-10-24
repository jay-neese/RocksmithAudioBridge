using System;
using System.IO;
using RSMods.Audio;

class SongLoudnessAuditTests
{
	static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
	static string Wave(string directory, int rate, double amplitude, bool right, int silence = 0)
	{
		string path = Path.Combine(directory, Guid.NewGuid().ToString("N") + ".wav");
		int frames = rate * (8 + silence);
		using (var stream = File.Create(path)) using (var writer = new BinaryWriter(stream))
		{
			writer.Write(0x46464952u); writer.Write(frames * 4 + 36); writer.Write(0x45564157u);
			writer.Write(0x20746d66u); writer.Write(16); writer.Write((short)1); writer.Write((short)2);
			writer.Write(rate); writer.Write(rate * 4); writer.Write((short)4); writer.Write((short)16);
			writer.Write(0x61746164u); writer.Write(frames * 4);
			for (int i = 0; i < frames; ++i)
			{
				short sample = i < rate * silence ? (short)0 : (short)Math.Round(amplitude * 32768 * Math.Sin(2 * Math.PI * 1000 * i / rate));
				writer.Write(sample); writer.Write(right ? sample : (short)0);
			}
		}
		return path;
	}
	static double Response(double b0, double b1, double b2, double a1, double a2)
	{
		double w = 2 * Math.PI * 1000 / 48000;
		double nr = b0 + b1 * Math.Cos(w) + b2 * Math.Cos(2 * w), ni = -b1 * Math.Sin(w) - b2 * Math.Sin(2 * w);
		double dr = 1 + a1 * Math.Cos(w) + a2 * Math.Cos(2 * w), di = -a1 * Math.Sin(w) - a2 * Math.Sin(2 * w);
		return (nr * nr + ni * ni) / (dr * dr + di * di);
	}
	static void Main(string[] args)
	{
		Directory.CreateDirectory(args[0]);
		foreach (int rate in new[] { 44100, 48000 })
		{
			string a = Wave(args[0], rate, 0.1, true), b = Wave(args[0], rate, 0.05, true),
				c = Wave(args[0], rate, 0.1, false), d = Wave(args[0], rate, 0.1, true, 5);
			try
			{
				double level = SongLoudnessAudit.ReadWave(a).IntegratedLufs;
				Check(Math.Abs(level - SongLoudnessAudit.ReadWave(b).IntegratedLufs - 6.0206) < 0.01, "Gain scaling failed");
				Check(Math.Abs(level - SongLoudnessAudit.ReadWave(c).IntegratedLufs - 3.0103) < 0.01, "Stereo weighting failed");
				Check(Math.Abs(level - SongLoudnessAudit.ReadWave(d).IntegratedLufs) < 0.15, "Silence gating failed");
				if (rate == 48000)
				{
					// Independent frequency-response calculation from the standard 48 kHz
					// K-weighting coefficients, rather than the implementation's constructors.
					double expected = -0.691 + 10 * Math.Log10(0.01
						* Response(1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585)
						* Response(1, -2, 1, -1.99004745483398, 0.99007225036621));
					Check(Math.Abs(level - expected) < 0.01, "K-weighting calibration failed");
				}
			}
			finally { foreach (string path in new[] { a, b, c, d }) File.Delete(path); }
		}
		Console.WriteLine("Loudness audit tests passed");
	}
}
