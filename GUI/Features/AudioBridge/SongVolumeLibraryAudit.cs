using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Rocksmith2014PsarcLib.Psarc;
using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace RSMods.Audio
{
	internal static class SongVolumeLibraryAudit
	{
		public const string Command = "--song-volume-audit";
		public static bool TryRun(string[] args, out int exitCode)
		{
			exitCode = 0;
			if (args.Length == 0 || args[0] != Command) return false;
			if (args.Length != 3) { exitCode = 2; return true; }
			try { Scan(Path.GetFullPath(args[1]), Path.GetFullPath(args[2])); }
			catch (Exception error)
			{
				Console.Error.WriteLine(error); exitCode = 1;
				if (Directory.Exists(args[2])) File.WriteAllText(Path.Combine(args[2], "fatal-error.txt"), error.ToString());
			}
			return true;
		}

		private static void Scan(string game, string output)
		{
			Directory.CreateDirectory(output);
			string records = Path.Combine(output, "songs.jsonl"), progress = Path.Combine(output, "progress.txt");
			var completed = new ConcurrentDictionary<string, JObject>(StringComparer.Ordinal);
			if (File.Exists(records)) foreach (string line in File.ReadLines(records))
			{
				try { var row = JObject.Parse(line); if ((string)row["Status"] == "ok") completed.TryAdd((string)row["Id"], row); }
				catch (JsonException) { } // a terminated scan can leave a partial last row
			}
			// Keep JSONL rows independently readable when resuming a partial write.
			if (File.Exists(records) && new FileInfo(records).Length > 0) File.AppendAllText(records, "\n");
			var archives = Directory.GetFiles(Path.Combine(game, "dlc"), "*_p.psarc", SearchOption.AllDirectories).ToList();
			if (File.Exists(Path.Combine(game, "songs.psarc"))) archives.Add(Path.Combine(game, "songs.psarc"));
			int scanned = 0, measured = 0, reused = 0, failed = 0;
			var reportLock = new object();
			Action<JObject> writeRecord = row =>
			{
				lock (reportLock)
				{
					File.AppendAllText(records, row.ToString(Formatting.None) + "\n");
					File.WriteAllText(progress, "Archives " + scanned + "/" + archives.Count + "; analyzed " + measured
						+ "; resumed " + reused + "; failures " + failed + "\n" + (string)row["Archive"] + "\n" + (string)row["Bank"] + "\n");
				}
			};
			Parallel.ForEach(archives, new ParallelOptions { MaxDegreeOfParallelism = Math.Min(4, Environment.ProcessorCount) }, archivePath =>
			{
				try
				{
					var info = new FileInfo(archivePath);
					using (var archive = new PsarcFile(archivePath))
					{
						var banks = archive.TOC.Entries.Select(e => PsarcEntryPath.TryGetFileName(e.Path, out string name) ? name : "")
							.Where(n => n.StartsWith("song_", StringComparison.OrdinalIgnoreCase)
								&& n.EndsWith(".bnk", StringComparison.OrdinalIgnoreCase)
								&& !n.EndsWith("_preview.bnk", StringComparison.OrdinalIgnoreCase)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
						if (banks.Count == 0)
						{
							bool unreadable = archive.TOC.Entries.Skip(1).Any(e => string.IsNullOrWhiteSpace(e.Path) || e.Path.Any(char.IsControl));
							bool hasAudio = archive.TOC.Entries.Any(e => e.Path != null && (e.Path.EndsWith(".wem", StringComparison.OrdinalIgnoreCase)
								|| e.Path.EndsWith(".bnk", StringComparison.OrdinalIgnoreCase)));
							if (unreadable || hasAudio)
								throw new InvalidDataException("No full-song banks could be enumerated; archive names or bank layout are unsupported by the current reader. No audio was analyzed.");
							writeRecord(new JObject { ["Status"] = "non-song-package", ["Archive"] = archivePath,
								["Note"] = "Readable directory contains no WEM audio or soundbanks (for example, compatibility metadata)." });
						}
						foreach (string bank in banks)
						{
							string id = "lufs-audit-v1|" + info.FullName + "|" + info.Length + "|" + info.LastWriteTimeUtc.Ticks + "|" + bank.ToLowerInvariant();
							if (completed.TryGetValue(id, out JObject previous))
							{
								// The id above verifies this audit row still describes this exact source.
								var source = SpeakerModeCacheExtractor.FindSourceInArchive(archive, archivePath, bank);
								string seed = new SongVolumeMeasurement { SampleRate = (uint)previous["SampleRate"], Frames = (ulong)previous["Frames"],
									Rms = (double)previous["Rms"], Peak = (double)previous["Peak"], BankDb = (double)previous["BankDb"],
									IntegratedLufs = (double)previous["IntegratedLufs"] }.Serialize();
								SongVolumeAnalyzer.Measure(source, "Song_" + (string)previous["SongKey"] + ".bnk",
									Path.Combine(output, "resume-" + Guid.NewGuid().ToString("N") + ".wav"), auditedMeasurement: seed);
								Interlocked.Increment(ref reused); continue;
							}
							var row = new JObject { ["Id"] = id, ["Archive"] = info.FullName, ["Bank"] = bank };
							string wave = Path.Combine(output, "decode-" + Guid.NewGuid().ToString("N") + ".wav");
							try
							{
								string key = bank.Substring(5, bank.Length - 9);
								row["SongKey"] = key;
								var manifest = archive.TOC.Entries.FirstOrDefault(e =>
									PsarcEntryPath.TryGetFileName(e.Path, out string name) && name.StartsWith(key + "_", StringComparison.OrdinalIgnoreCase)
									&& e.Path.EndsWith(".json", StringComparison.OrdinalIgnoreCase));
								if (manifest != null)
								{
									try
									{
										using (var memory = new MemoryStream())
										{
											archive.InflateEntry(manifest, memory); memory.Position = 0;
											using (var reader = new StreamReader(memory))
											{
												var entries = JObject.Parse(reader.ReadToEnd())["Entries"] as JObject;
												var attributes = entries?.Properties().FirstOrDefault()?.Value["Attributes"];
												row["Title"] = attributes?["SongName"];
												row["Artist"] = attributes?["ArtistName"];
												string authoredKey = (string)attributes?["SongKey"];
												if (string.Equals(key, authoredKey, StringComparison.OrdinalIgnoreCase)) key = authoredKey;
											}
										}
									}
									catch (Exception error) { row["MetadataWarning"] = error.Message; }
								}
								row["SongKey"] = key;
								var source = SpeakerModeCacheExtractor.FindSourceInArchive(archive, archivePath, bank);
								if (source == null) throw new InvalidDataException("Soundbank not found.");
								SongLoudnessAudit loudness = null;
								string text = SongVolumeAnalyzer.Measure(source, "Song_" + key + ".bnk", wave,
									measurement => loudness = measurement);
								string[] fields = text.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
								var ci = CultureInfo.InvariantCulture;
								row["SampleRate"] = uint.Parse(fields[1], ci); row["Frames"] = ulong.Parse(fields[2], ci);
								row["Rms"] = double.Parse(fields[3], ci); row["Peak"] = double.Parse(fields[4], ci);
								row["BankDb"] = double.Parse(fields[5], ci);
								row["IntegratedLufs"] = loudness.IntegratedLufs;
								row["ShortTermP10"] = loudness.ShortTermP10; row["ShortTermP95"] = loudness.ShortTermP95;
								row["Status"] = "ok"; completed.TryAdd(id, row); Interlocked.Increment(ref measured);
							}
							catch (Exception error) { row["Status"] = "error"; row["Error"] = error.ToString(); Interlocked.Increment(ref failed); }
							finally { if (File.Exists(wave)) File.Delete(wave); }
							writeRecord(row);
						}
					}
				}
				catch (Exception error)
				{
					Interlocked.Increment(ref failed);
					writeRecord(new JObject { ["Status"] = "archive-error", ["Archive"] = archivePath,
						["Error"] = error.ToString() });
				}
				lock (reportLock)
				{
					string status = "Archives " + (++scanned) + "/" + archives.Count + "; analyzed " + measured + "; resumed " + reused + "; failures " + failed;
					File.WriteAllText(progress, status + "\n" + archivePath + "\n");
				}
			});
			File.AppendAllText(progress, "Complete.\n");
		}
	}
}
