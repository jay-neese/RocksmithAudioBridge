using System;
using System.Globalization;
using RSMods.Audio;

class SongVolumeMeasurementTests
{
	static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
	static void Main()
	{
		const string valid = "3 44100 11457683 0.087835515804253986 0.660125732421875 0.5 -18.25\n";
		// Cache format is independent of the user's Windows decimal separator.
		CultureInfo.CurrentCulture = new CultureInfo("de-DE");
		Check(SongVolumeMeasurement.TryParse(valid, out var measurement), "Valid profile rejected");
		Check(measurement.IntegratedLufs == -18.25 && measurement.BankDb == 0.5, "Profile values changed");
		Check(SongVolumeMeasurement.TryParse(measurement.Serialize(), out var roundTrip)
			&& roundTrip.Rms == measurement.Rms && roundTrip.Peak == measurement.Peak && roundTrip.IntegratedLufs == measurement.IntegratedLufs,
			"Profile did not round-trip exactly");
		foreach (string bad in new[] { null, "", "2 44100 11457683 .1 1 -8", "3 44100 11457683 .1 1 -8", "3 44100 -1 .1 1 -8 -12",
			"3 44100 18446744073709551615 .1 1 -8 -12", "3 22050 11457683 .1 1 -8 -12", "3 48000 20 .1 1 -8 -12",
			"3 48000 11457683 NaN 1 -8 -12", "3 48000 11457683 .1 Infinity -8 -12", "3 48000 11457683 .1 1 NaN -12",
			"3 48000 11457683 .1 1 -8 NaN", "3 48000 11457683 .1 1 -8 -71", "3 48000 11457683 .1 1 -8 13",
			"3 48000 11457683 .1 1 -8 -12 extra" })
			Check(!SongVolumeMeasurement.TryParse(bad, out _), "Invalid/old profile accepted: " + bad);
		Console.WriteLine("LUFS measurement protocol tests passed");
	}
}
