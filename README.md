# Rocksmith Audio Bridge

![Rocksmith Audio Bridge](.github/images/rocksmith-audio-bridge-banner.jpg)

<br>

An extension of [RSMods](https://github.com/Lovrom8/RSMods) for Rocksmith 2014
Remastered. Play any song without retuning, run your own amp sim, record your
playing, and practise note by note, with stable audio on speakers and
headphones Rocksmith can't normally use. Everything is controlled from an in-game
overlay.

Formerly RSModsPlus, renamed because the old name was easily mistaken for
Rocksmith+ and its expanded into a suite of features now.

---

<p align="center">
  <a href="https://buymeacoffee.com/cheesewizard">
    <img src=".github/images/buy-me-a-beer-alt-amplifier-v2.png" alt="Buy me a beer" width="200">
  </a>
</p>

<p align="center">
  I build this mod in my own time and I'm currently looking for work.<br>
  If it has helped you, please consider <a href="#support">supporting its development</a>. Thank you
</p>

---

## Demo

https://github.com/user-attachments/assets/1b51d057-285e-4ded-a113-5c25af1f3346

**Features**

- [Stable audio on any speakers](#stable-audio-on-any-speakers)
- [Drop Pedal](#drop-pedal)
- [Speaker Mode](#speaker-mode)
- [Note by Note (beta)](#note-by-note-beta)
- [Tone designer](#tone-designer)
- [External amp](#external-amp)
- [Recording](#recording)
- [Mixer](#mixer)
- [Guitar input cleanup](#guitar-input-cleanup)
- [Output switching](#output-switching)

## Install

1. Close Rocksmith and RSMods.
2. Download `RocksmithAudioBridge-Installer.exe` from the
   [latest release](https://github.com/Cheesewizard/RocksmithAudioBridge/releases).
3. Run it and press **Install**. Accept the Windows admin prompt; it registers
   the Audio Bridge ASIO driver and installs UsbDk, which the Real Tone Cable's
   direct USB read uses.
4. **Restart your PC** when the installer asks. UsbDk only starts after a
   restart; until then the cable still works, but with Windows' usual delay.

The installer includes the full RSMods suite, so you do not need to install
RSMods first. Run it again to **Reinstall / Repair** (for example after a Steam
file check) or **Uninstall**. Uninstall asks before removing your settings and
UsbDk, and never touches your recordings.

Requirements: the Steam version of Rocksmith 2014 Remastered on Windows, and
the MS Visual C++ 2015-2019 redistributable. Works with a Real Tone Cable or an
ASIO interface through [RS_ASIO](https://github.com/mdias/rs_asio).

**ASIO users need RS_ASIO 0.6.1 or newer**; the
[latest release](https://github.com/mdias/rs_asio/releases/latest) is
recommended. Your version is on the first line of `RS_ASIO-log.txt`. To update,
replace `RS_ASIO.dll` and `avrt.dll` and keep your `RS_ASIO.ini`.

### After installing

With Rocksmith closed, open RSMods (`RSMods\RSMods.exe` in your Rocksmith
folder) and go to the **Rocksmith Audio Bridge** tab. Set **Guitar input** to
**Real Tone Cable** or **ASIO interface** to match how your guitar is
connected.

<img src=".github/images/rsmods-audio-bridge-setup.png" alt="The Rocksmith Audio Bridge tab in RSMods" width="600">

With an ASIO interface, the **ASIO bridge driver** row shows that the driver is
installed and which interface it is using.

The **Audit** tab checks the basics for your guitar input (Rocksmith's audio
settings, RS_ASIO or the cable) and offers a fix where it can. The RS_ASIO
Settings tab shows its result on a small Audit card. If something goes wrong,
**Make bug report** on the Rocksmith Audio Bridge tab zips your logs and
settings and opens an email to the developer.

## The overlay

Press `\` in game to open the Audio Bridge overlay. Each feature below says
which page of it to use. Overlay settings are saved to `RSMods.ini`.

Keys can be changed in RSMods.exe, on the Keybindings tab. Upgrading from an
earlier version keeps the keys you already had.

---

## Stable audio on any speakers

<img src=".github/images/no-audio-output-device.png" alt="Rocksmith's No audio output device error" width="450">

"No audio output device" is the most common reason a fresh Rocksmith install
won't start. The Audio Bridge gives Rocksmith its own input and output devices,
so one badly behaved Windows playback device can't stop the game from starting,
even with no speakers connected at all.

Rocksmith always sees the standard 48 kHz stereo device it expects, and the
Audio Bridge converts to whatever your real speakers or headphones use. So
devices at other sample rates (such as 44.1 kHz) or with surround layouts (such
as 5.1) play without errors or crackling. Sound goes to your Windows default
playback device, or to whichever one you pick in
[Output switching](#output-switching).

**Needs:** nothing with a Real Tone Cable. With an ASIO interface, set the
Audio Bridge driver in `RS_ASIO.ini` (**Set up for me** on the Audit tab can do
it for you).

## Drop Pedal

Shifts your **guitar** to match the song, from -24 to +24 semitones, without
touching a tuning peg. Note detection, the tuner and scoring follow the shift,
and any tone works. In multiplayer each player has their own shift and base
tuning; a player who doesn't need a shift leaves theirs at 0.

To play an Eb song on an E-standard guitar, press `F7` once, then `,` once.

| Action | Player 1 | Player 2 |
|---|---|---|
| Cycle Drop Pedal / Speaker Mode / Off (both players) | `F7` | |
| Shift down / up one semitone | `,` / `.` | `Ctrl+,` / `Ctrl+.` |
| Set the guitar's physical tuning | `F9` | `Ctrl+F9` |

**Overlay page:** Drop Pedal (readout and colours).

## Speaker Mode

Shifts the **song** to match your guitar instead. Made for playing through
speakers, where you hear the guitar itself in the room. The song is
pitch-rendered ahead of time, so there is no added latency. Drop and open
tunings work too: the tuner tells you which strings to retune.

Press `F7` until the readout says `Speaker`, then use `,` and `.` as with the
Drop Pedal.

Speaker Mode is a global setting that only Player 1 can change; in
multiplayer, both players tune to Player 1's tuning. For separate settings per
player, use the Drop Pedal.

## Note by Note (beta)

A Riff Repeater practice mode that checks every note you play. Detection
combines the game's own matcher, a raw pitch verifier and a bundled
machine-learning detector, which starts on its own.

- **Flow mode** (on by default): the song keeps playing while you hit the
  notes, and stops at the first note you miss until you play it. The Riff
  Repeater speed is capped to what detection can keep up with.
- **Flow mode off:** the song waits at every note until you play it.

Turn it on in game with the **NOTE BY NOTE** switch in Riff Repeater Advanced
Settings; this menu is the only place to switch it on. The **FLOW MODE** switch
sits just below it.

| Action | Key |
|---|---|
| Emergency skip, if you ever get stuck on a note | `Right Arrow` |

**Overlay page:** Note by Note (settings only: readout, target style and size,
colours, and dragging the on-screen readout into place).

Works on guitar and bass arrangements. Bass support is experimental: it uses
the game's own detection with a bass-trained machine-learning model.

## Tone designer

Play through your own VST3 amp sims and pedals instead of Rocksmith's amp. The
plugins run in a separate 64-bit host, so 64-bit plugins work in the 32-bit game
and a plugin that crashes never takes the game with it. Build a chain of up to
8 plugins, save it as a tone, and give songs their own tone that loads when the
song starts.

Works with an ASIO interface and with the Real Tone Cable. With the cable, the
bridge reads it directly over USB through UsbDk when it can, about 10 ms sooner
than the Windows cable input, and uses the Windows input otherwise.

**Overlay page:** Amp sim, Tone designer tab.

## External amp

Play through your own amp sim, such as AmpliTube 5, instead of Rocksmith's amp,
while the game keeps the clean signal for note detection. It shares your
interface with the game, adding one buffer of latency at your interface's
buffer size. When the amp sim closes, the game's amp comes back. The
[Drop Pedal](#drop-pedal) works with it too: the amp sim gets your shifted
guitar.

In the amp sim's audio settings, pick **Rocksmith Audio Bridge ASIO** as both
the input and output device. Set the input channels to **Rocksmith guitar** and
the outputs to **Rocksmith out L** and **Rocksmith out R**.

<img src=".github/images/external-amp-amplitube-setup.png" alt="AmpliTube 5 audio settings using Rocksmith Audio Bridge ASIO" width="600">

**Overlay page:** Amp sim, External app tab (status, return level). The safety
buffer is on the page's Amp link card.

**Needs:** an ASIO interface with the Audio Bridge driver set as the `Driver` in
`RS_ASIO.ini`. **Set up for me** on the settings app's Audit tab can do this: it
backs up your file first, and **Restore** puts the original back. Nothing else
in the mod changes `RS_ASIO.ini` on its own, apart from a fresh install setting
up a file nobody has set up yet (backed up the same way).

## Recording

Record what you play without leaving the game. An audio take saves the full
game mix and your dry guitar together as a pair. A video take saves an MP4 with
the game sound.

The recording is exactly what you hear: the song, your guitar and, with
[External amp](#external-amp), your amp sim's tone. It works the same whichever
playback device the sound goes to. The dry take is your clean guitar, ready to
re-amp later. Each game launch saves into its own dated folder under
`Recordings` in your Rocksmith folder. Pick another folder on the Rocksmith Audio
Bridge page in RSMods. For video, the overlay's Record page sets the quality:
**High** (sharpest), **Standard** (about half the size) or **Small** (1080p, for
sharing).

Press `F6`, or the record button on the overlay's Record page. A red REC
indicator shows while recording.

**Overlay page:** Record (audio or video, the record key).

**Needs:** with an ASIO interface, the Audio Bridge driver set in
`RS_ASIO.ini` to record the game mix.

## Mixer

Seven volume faders: Master, Player 1, Player 2, Song, Microphone, Voice-over
and Effects. Player levels change only what you hear, never note detection.
Levels reset when the game closes.

**Overlay page:** Mixer.

Open the in-game overlay with **\\**, then choose **Output > Song volume**.
Enable **Normalize song volume** to keep backing tracks at similar loudness as
you move between songs. Guitar, voice, effects and note detection stay independent.
Set the Mixer Song fader once to balance backing tracks against your guitar.

There are two controls:

- **Song loudness:** move right for louder backing tracks. It applies to every
  song. The readout uses LUFS, but you can adjust by ear. Start around -21 LUFS
  and raise it gradually to suit your guitar. With compression allowed the range
  is -36 to -4 LUFS. With compression off, it adapts to the current song's
  reachable peak/gain limits, removing sections where moving it would have no
  effect. For example, Interstate Love Song stops around -20.6 LUFS without
  compression. Each displayed 0.1 LUFS step in that usable range changes gain.
- **Allow peak compression:** off preserves dynamics and may leave songs below
  the chosen loudness. On reduces loud peaks as needed so songs can get louder
  toward that setting. It does not add a fixed number of decibels. High loudness
  settings can sound less dynamic.

If raising Song loudness stops changing a track's volume, the status says it is
at its peak limit and suggests turning on Allow peak compression. The status
also reports when the limiter is actively reducing peaks or when a gain bound
prevents reaching the chosen loudness. The technical gain calculation and bank
volume are kept in the debug log rather than the main controls.
When the current song cannot reach a previously chosen global setting, the
slider displays the reachable level and says that the original setting is
saved. Changing songs or turning compression off never silently overwrites
that saved setting. Allow compression to use the wider range, or drag the
slider to choose a new global value. At very high settings, the compressor's
output ceiling still limits how much additional average volume is possible.

The RMS/LUFS selector and Song boost slider have been removed. The overlay uses
LUFS. Both the loudness setting and compression permission persist across songs
and restarts; these are not per-song adjustments.

### Upgrading saved settings

Existing LUFS targets are retained. A former boost is folded into the loudness
setting and turns compression permission on: for example, **-14 LUFS plus +2 dB
boost becomes -12 LUFS with compression allowed**. The expanded slider covers
the former LUFS-target-plus-boost range. Legacy RMS settings are translated to an
approximately comparable LUFS target (RMS target minus 9 dB). Legacy keys and RMS
calculations remain readable internally for compatibility and validation.
Migration is idempotent; saving the new controls clears legacy boost and selects
LUFS, so restarting does not add the old boost again.

`SongVolumeLufsTarget` stores the single target in tenths of LUFS.
`SongVolumePeakCompression` stores compression permission (0 or 1).
Compression defaults off unless it was enabled explicitly or a saved legacy
boost was positive. A fresh installation starts around -21 LUFS with compression
off. Install the matching game DLL and analyzer together.

### How song leveling works

Each supported song gets one fixed correction based on its measured integrated
loudness and authored soundbank volume. Quiet and loud passages within a song
remain distinct, except where peak compression intervenes. A short gain ramp
handles setting changes without an abrupt jump. There is no added playback
latency. The limiter has immediate attack and 80 ms release, and only processes
backing-track audio.

The first encounter starts background analysis; original volume plays until it
is ready. Subsequent encounters reuse the cache in
`%LOCALAPPDATA%\Rocksmith Audio Bridge\SongVolume`. Cache version 3 stores RMS,
peak, bank volume and LUFS. Matching library-audit results can populate it without
another decode. An updated song archive invalidates the measurement. Temporary
WAVs are removed after analysis.

The analyzer reuses Speaker Mode's WEM extractor but works with Speaker Mode and
Drop Pedal off. It matches gameplay audio by decoded sample rate and duration,
excluding menu previews, and bypasses failed or unsupported analyses. Bank gain
from -96 to +24 dB is supported. Positive bank gain gets additional decoder
headroom so it does not undo peak protection later in Wwise.

Requested gain is bounded to +22 dB (the former +12 dB normalization gain plus
+10 dB boost). With compression off, measured sample peaks further limit the
fixed gain. With it on, a stereo-linked limiter controls peaks before conversion
instead, allowing more average volume toward the same target.
Both paths bound requested attenuation to -24 dB; peak protection may require
more attenuation. True peaks are not oversampled, and pitch shifting can change
peaks and loudness. Compression cannot guarantee the exact target on every track.

LUFS measurement uses BS.1770-style stereo K weighting, 400 ms blocks with 75%
overlap, a -70 LUFS absolute gate, and -10 LU relative gate. Synthetic calibration
and cache checks support it; this is not a certified loudness meter. In-game
listening is still needed to assess guitar balance.

Debug logging records measured LUFS, matched playback, requested gain,
`compressionAllowed`, and `peakReductionDb`. While compression is allowed,
matched playback reports current limiter reduction about every five seconds.
Requested gain is not a measurement of achieved loudness after limiting.
The independent **Output > Volume cap** controls final-output peaks with its
existing look-ahead latency. Retired whole-mix leveling remains disabled.

### Audit or pre-analyze a whole song library

The hidden helper command below scans full-song banks in `dlc/**/*_p.psarc`
and `songs.psarc`, excludes menu previews, and warms the combined RMS/LUFS measurement
cache used in game. It does not change settings or song archives. Up to four
workers decode temporary WAVs; allow disk space for several decoded songs.

```powershell
& .\RSMods.exe --song-volume-audit 'C:\path\to\Rocksmith' 'C:\path\to\audit-output'
```

Use the installed `RSMods\RSMods.exe`, or a freshly built copy in
`Installer\Resources\RSModsGUI`. Because this is a GUI executable, use
`Start-Process -Wait` from PowerShell when waiting for completion in a script.
`progress.txt` reports progress; `songs.jsonl` records each result. Rerunning with
the same output directory reuses completed audit results for unchanged source
archives and retries failures. Different archive versions are separate results;
use a new output directory for a clean inventory after library changes.
Unreadable archive directories and unsupported bank layouts are reported as
coverage gaps. Readable packages containing no audio or soundbanks (such as
compatibility metadata) are reported separately, not as song failures. They are
never counted as analyzed songs. Cache bank names are case-independent. Rerunning
a completed audit seeds version 3 measurements from its source-validated rows
without decoding the tracks again. Older RMS-only caches need a matching audit
or a one-time decode to add LUFS. Ambiguous WEM byte
matches are resolved with the soundbank parser's source ID when available.

The additional LUFS-style audit measures stereo K-weighted integrated loudness
with overlapping gates and 3-second short-term percentiles. It is diagnostic,
not a change to the RMS normalization algorithm or a certified loudness meter.
It does not measure oversampled true peaks or live decoder-hook matching.
With Python installed, generate a CSV and Markdown report from the repository:

```powershell
python scripts/report_song_volume_audit.py 'C:\path\to\audit-output' --target -12
```

The report models the historical RMS baseline and LUFS with compression off,
highlights peak/gain limits, and compares loudness distributions at the same
median level. It does not simulate the live compressor or predict achieved
loudness with compression on. A report produced during scanning is partial; rerun
the Python command after `progress.txt` says `Complete.` for final coverage.

## Guitar input cleanup

Fixes a quiet or noisy guitar signal before Rocksmith hears it: make-up gain, a
noise suppressor that keeps your sustain, a compressor, a hum filter that
removes only the mains hum, and an override for Rocksmith's own noise gate.
Works with a Real Tone Cable or an ASIO interface.

**Overlay page:** Input (with a live level meter).

## Output switching

Move the game's sound to another playback device while you play, with no
restart. The change lasts until you close Rocksmith. An optional limiter caps
the maximum volume.

**Overlay page:** Output.

---

Everything else (extended range, custom song list titles, toggle loft and the
rest) comes from RSMods and works as
[upstream documents it](https://github.com/Lovrom8/RSMods#readme). The overlay's
General page can also rescan the song list after you add a new song, without
restarting.

## Support

Rocksmith Audio Bridge is free, and I develop it entirely in my own time while
looking for work. If the mod has made Rocksmith better for you, a beer goes a
long way towards keeping it going. Thank you!

<a href="https://buymeacoffee.com/cheesewizard">
  <img src=".github/images/buy-me-a-beer-alt-amplifier-v2.png" alt="Buy me a beer" width="200">
</a>

## Reporting problems

The quickest way is **Make bug report** on the settings app's Rocksmith Audio
Bridge tab. Or report bugs [here](https://github.com/Cheesewizard/RocksmithAudioBridge/issues), not on
the RSMods tracker. A bug in an inherited RSMods feature that also happens on
plain RSMods belongs [upstream](https://github.com/Lovrom8/RSMods/issues).

Attach the logs from `%LOCALAPPDATA%\Rocksmith Audio Bridge\Logs` and `RSMods_debug.txt` from
the Rocksmith folder. With an ASIO interface, also attach `RS_ASIO-log.txt` and `RS_ASIO.ini`. The debug log is locked while the game runs, so quit
first.

## Credits

RSMods is the work of **Lovrom8** and **ffio1**, with contributions from
ZagatoZee, Kokolihapihvi and L0fka.
[RS_ASIO](https://github.com/mdias/rs_asio) by **mdias** makes the ASIO path
possible. [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
by **Signalsmith Audio** does Speaker Mode's pitch shifting. Full third-party
notices are in [NOTICE](NOTICE).

Code written for this project is MIT licensed ([LICENSE](LICENSE)). RSMods code
remains its authors' property.
