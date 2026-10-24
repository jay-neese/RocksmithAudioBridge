using System;
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
		private static string CacheKey(string identity)
		{
			using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(identity))).Replace("-", "");
		}
		private static bool ValidMeasurement(string text)
		{
			return SongVolumeMeasurement.TryParse(text, out _);
		}
		public static bool TryRun(string[] args, out int exitCode)
		{
			exitCode = 0;
			if (args.Length == 0 || args[0] != Command) return false;
			if (args.Length != 5) { exitCode = 2; return true; }
			string wave = args[3] + ".wav";
			try
			{
				var source = SpeakerModeCacheExtractor.FindSource(args[1], args[2]);
				File.WriteAllText(args[3], Measure(source, args[2], wave));
			}
			catch (Exception error) { File.WriteAllText(args[4], error.ToString()); exitCode = 1; }
			return true;
		}

		// The audit supplies an already indexed source, avoiding a full library search
		// for every song. The callback observes the temporary decoded WAV before cleanup.
		internal static string Measure(SpeakerCacheSource source, string bankName, string wave,
			Action<SongLoudnessAudit> inspectLoudness = null, string auditedMeasurement = null)
		{
			string bank = wave + ".bnk";
			try
			{
				var archive = new FileInfo(source.ArchivePath);
				// Include installation, source archive revision, media entry, and algorithm version.
				string identity = "gated-rms-bank-lufs-v3\n" + archive.FullName + "\n" + archive.Length + "\n"
					+ archive.LastWriteTimeUtc.Ticks + "\n" + source.WemEntryPath + "\n";
				string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Rocksmith Audio Bridge", "SongVolume");
				Directory.CreateDirectory(directory);
				// Bank lookup is case-insensitive. Cache lookup must be too: manifest
				// keys and base-game bank filenames can use different capitalization.
				string cached = Path.Combine(directory, CacheKey(identity + bankName.ToLowerInvariant()) + ".txt");
				string text = File.Exists(cached) ? File.ReadAllText(cached) : "";
				bool saveCache = !ValidMeasurement(text);
				// Only the library audit supplies this seed, after matching its source
				// archive path, length, write time and bank name to the current archive.
				if (saveCache && auditedMeasurement != null && ValidMeasurement(auditedMeasurement)) text = auditedMeasurement;
				SongLoudnessAudit loudness = null;
				if (!ValidMeasurement(text))
				{
					File.WriteAllBytes(bank, source.BankBytes);
					float? bankDb = SoundBankGenerator2014.ReadVolumeFactor(bank, new Platform(GamePlatform.Pc, GameVersion.RS2014));
					if (!bankDb.HasValue || float.IsNaN(bankDb.Value) || float.IsInfinity(bankDb.Value)
						|| bankDb.Value < -96 || bankDb.Value > 24)
						throw new InvalidDataException("Could not read the backing track's soundbank volume; normalization bypassed.");
					SpeakerModeCacheExtractor.Extract(source, wave);
					SongLevelAnalysis analysis;
					using (var stream = File.OpenRead(wave)) analysis = SongLevelAnalysis.ReadWave(stream);
					loudness = SongLoudnessAudit.ReadWave(wave);
					text = new SongVolumeMeasurement { SampleRate = analysis.SampleRate, Frames = analysis.Frames,
						Rms = analysis.Rms, Peak = analysis.Peak, BankDb = bankDb.Value, IntegratedLufs = loudness.IntegratedLufs }.Serialize();
					if (!ValidMeasurement(text)) throw new InvalidDataException("Unsupported song measurement.");
				}
				if (saveCache)
				{
					string temporary = cached + "." + Guid.NewGuid().ToString("N");
					try
					{
						File.WriteAllText(temporary, text);
						if (File.Exists(cached)) File.Delete(cached);
						File.Move(temporary, cached);
					}
					finally { if (File.Exists(temporary)) File.Delete(temporary); }
				}
				if (inspectLoudness != null)
				{
					if (loudness == null)
					{
						if (!File.Exists(wave)) SpeakerModeCacheExtractor.Extract(source, wave);
						loudness = SongLoudnessAudit.ReadWave(wave);
					}
					inspectLoudness(loudness);
				}
				return text;
			}
			finally
			{
				if (File.Exists(wave)) File.Delete(wave);
				if (File.Exists(bank)) File.Delete(bank);
			}
		}
	}
}
