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
