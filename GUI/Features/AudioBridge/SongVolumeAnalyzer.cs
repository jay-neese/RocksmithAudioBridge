using System;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using RocksmithToolkitLib;
using RocksmithToolkitLib.DLCPackage;

namespace RSMods.Audio
{
	internal static class SongVolumeAnalyzer
	{
		public const string Command = "--song-volume-analyze";
		private static bool ValidMeasurement(string text)
		{
			string[] fields = text.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
			var ci = CultureInfo.InvariantCulture;
			return fields.Length == 6 && fields[0] == "2"
				&& uint.TryParse(fields[1], NumberStyles.Integer, ci, out uint rate) && (rate == 44100 || rate == 48000)
				&& ulong.TryParse(fields[2], NumberStyles.Integer, ci, out ulong frames) && frames >= rate * 20UL
				&& double.TryParse(fields[3], NumberStyles.Float, ci, out double rms)
				&& double.TryParse(fields[4], NumberStyles.Float, ci, out double peak)
				&& rms > 0 && peak >= rms && peak <= 1
				&& double.TryParse(fields[5], NumberStyles.Float, ci, out double bankDb)
				&& bankDb >= -96 && bankDb <= 24;
		}
		public static bool TryRun(string[] args, out int exitCode)
		{
			exitCode = 0;
			if (args.Length == 0 || args[0] != Command) return false;
			if (args.Length != 5) { exitCode = 2; return true; }
			string wave = args[3] + ".wav";
			string bank = args[3] + ".bnk";
			try
			{
				var source = SpeakerModeCacheExtractor.FindSource(args[1], args[2]);
				var archive = new FileInfo(source.ArchivePath);
				// Include installation, source archive revision, media entry, and algorithm version.
				string identity = "gated-rms-bank-v2\n" + archive.FullName + "\n" + archive.Length + "\n"
					+ archive.LastWriteTimeUtc.Ticks + "\n" + source.WemEntryPath + "\n" + args[2];
				string key;
				using (var sha = SHA256.Create()) key = BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(identity))).Replace("-", "");
				string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Rocksmith Audio Bridge", "SongVolume");
				Directory.CreateDirectory(directory);
				string cached = Path.Combine(directory, key + ".txt");
				string text = File.Exists(cached) ? File.ReadAllText(cached) : "";
				if (!ValidMeasurement(text))
				{
					File.WriteAllBytes(bank, source.BankBytes);
					float? bankDb = SoundBankGenerator2014.ReadVolumeFactor(bank, new Platform(GamePlatform.Pc, GameVersion.RS2014));
					if (!bankDb.HasValue || float.IsNaN(bankDb.Value) || float.IsInfinity(bankDb.Value)
						|| bankDb.Value < -96 || bankDb.Value > 24)
						throw new InvalidDataException("Could not read the backing track's soundbank volume; normalization bypassed.");
					SpeakerModeCacheExtractor.Extract(args[1], args[2], wave);
					SongLevelAnalysis analysis;
					using (var stream = File.OpenRead(wave)) analysis = SongLevelAnalysis.ReadWave(stream);
					text = string.Format(CultureInfo.InvariantCulture, "2 {0} {1} {2:R} {3:R} {4:R}\n", analysis.SampleRate, analysis.Frames, analysis.Rms, analysis.Peak, bankDb.Value);
					if (!ValidMeasurement(text)) throw new InvalidDataException("Unsupported song measurement.");
					string temporary = cached + "." + Guid.NewGuid().ToString("N");
					try
					{
						File.WriteAllText(temporary, text);
						if (File.Exists(cached)) File.Delete(cached);
						File.Move(temporary, cached);
					}
					finally { if (File.Exists(temporary)) File.Delete(temporary); }
				}
				File.WriteAllText(args[3], text);
			}
			catch (Exception error) { File.WriteAllText(args[4], error.ToString()); exitCode = 1; }
			finally
			{
				if (File.Exists(wave)) File.Delete(wave);
				if (File.Exists(bank)) File.Delete(bank);
			}
			return true;
		}
	}
}
