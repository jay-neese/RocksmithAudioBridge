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
