Run the standalone audio leveling regressions from the repository root:

```powershell
cmake -S DLL/AsioProxy/tests -B build/leveling-tests-cmake
cmake --build build/leveling-tests-cmake --config Release
ctest --test-dir build/leveling-tests-cmake -C Release --output-on-failure
```

These tests exercise the shared DSP used by both ASIO and Windows output.
They cover convergence across loud and quiet signals, stereo balance, silence,
startup, boost/cut limits, live target updates, bypass, and the optional limiter.
They do not measure perceived loudness or replace listening tests in Rocksmith.

`SongVolumeTests` checks fixed per-song gain, target matching, peak/boost limits,
stream matching, stereo balance, preserved dynamics, ramping, and exact bypass.
It also tests +10 dB song boost, limiting of transients at 44.1/48 kHz, an increase
in average level, stereo linking, independence from callback size, and smooth
restoration of exact bypass when boost is disabled.
Bank-aware tests cover negative, zero, and positive authored gain, including
the +0.5 dB used by "No Sleep Till Brooklyn". They check the downstream peak
ceiling during startup ramps and boost at 44.1/48 kHz, including +24 dB bank
gain, and verify independence from callback size.
Run the offline WAV measurement tests from a Visual Studio Developer PowerShell:

```powershell
csc /nologo /out:build/leveling-tests/SongLevelAnalysisTests.exe GUI/Features/AudioBridge/SongLevelAnalysis.cs DLL/AsioProxy/tests/SongLevelAnalysisTests.cs
& ./build/leveling-tests/SongLevelAnalysisTests.exe
```

They cover known RMS/peak values, silence gating, quiet passages, stream metadata,
silent files, and truncated WAV rejection. Full extraction and decoder-hook
compatibility still require game testing, especially CDLC and Speaker Mode.

The optional library-audit loudness measurement has independent calibration,
gain scaling, stereo weighting, and silence-gating checks at 44.1/48 kHz:

```powershell
csc /nologo /optimize+ /out:build/leveling-tests/SongLoudnessAuditTests.exe GUI/Features/AudioBridge/SongLoudnessAudit.cs DLL/AsioProxy/tests/SongLoudnessAuditTests.cs
& ./build/leveling-tests/SongLoudnessAuditTests.exe build/song-audit-tests
```

`SongVolumeModeTests` verifies matching across RMS/LUFS modes, authored gain,
peak and gain bounds, mode/target atomic snapshots, legacy and current control
commands, strict version 3 profile parsing, and smooth A/B switching at 44.1/48 kHz.
It also covers the simplified `L,on,target,compression` command, migration from
legacy boost/RMS settings without repeated gain addition, the user's blink-182
peak-limit case, and no gain change from allowing compression when peaks do not
need reducing. The overlay no longer exposes the legacy mode selector or boost.
Adaptive-range checks cover the measured ceilings for Interstate Love Song and
blink-182, preserving a global target when the displayed value is limited,
unavailable measurements, and impossible intervals. The optional real-cache
pass checks that every displayed 0.1 LUFS step strictly increases gain with
compression off on every successfully measured track.
The cross-language measurement protocol also has a standalone C# check:

```powershell
csc /nologo /optimize+ /out:build/leveling-tests/SongVolumeMeasurementTests.exe GUI/Features/AudioBridge/SongVolumeMeasurement.cs DLL/AsioProxy/tests/SongVolumeMeasurementTests.cs
& ./build/leveling-tests/SongVolumeMeasurementTests.exe
```

To check native parsing and RMS/LUFS headroom calculations on real version 3
profiles after audit import, pass the cache directory to the mode test:

```powershell
& ./build/leveling-tests-cmake/Release/SongVolumeModeTests.exe "$env:LOCALAPPDATA\Rocksmith Audio Bridge\SongVolume"
```
