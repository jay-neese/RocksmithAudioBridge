"""Summarize --song-volume-audit JSONL without changing game settings or DSP.

Usage: python scripts/report_song_volume_audit.py AUDIT_DIRECTORY --target -12
Uses only the Python standard library. Re-running during a scan is safe.
"""
import argparse
import collections
import csv
import json
import math
from pathlib import Path
import statistics


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return math.nan
    position = (len(values) - 1) * fraction
    left = int(position)
    right = min(left + 1, len(values) - 1)
    return values[left] + (values[right] - values[left]) * (position - left)


def db(value):
    return 20 * math.log10(value)


def bounded_gain(wanted_db, peak, bank):
    bound = min(12, max(-24, wanted_db))
    peak_cap = -1 - db(peak) - max(0, bank)
    actual = min(bound, peak_cap)
    reason = "peak" if peak_cap < bound - 0.001 else "gain" if abs(bound - wanted_db) > 0.001 else "none"
    return actual, reason


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--target", type=float, default=-12)
    args = parser.parse_args()
    if not -30 <= args.target <= -12:
        parser.error("target must be between -30 and -12 dBFS RMS")
    latest = {}
    archive_errors = {}
    non_song_archives = {}
    for line in (args.directory / "songs.jsonl").read_text(encoding="utf-8-sig").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "Id" in row:
            latest[row["Id"]] = row
            # A later per-song result demonstrates that archive enumeration
            # succeeded on retry; retain per-song failures independently.
            archive_errors.pop(row.get("Archive"), None)
        elif row.get("Status") == "archive-error":
            archive_errors[row["Archive"]] = row
            non_song_archives.pop(row["Archive"], None)
        elif row.get("Status") == "non-song-package":
            non_song_archives[row["Archive"]] = row
            archive_errors.pop(row["Archive"], None)
    good = [r for r in latest.values() if r["Status"] == "ok"]
    failed = [r for r in latest.values() if r["Status"] != "ok"]
    if not good:
        raise SystemExit("No completed songs yet")
    summaries = []
    for row in good:
        result = {key: row.get(key, "") for key in ("Artist", "Title", "SongKey", "Archive", "Bank")}
        result.update(SampleRate=row["SampleRate"], Frames=row["Frames"],
                      DurationSeconds=row["Frames"] / row["SampleRate"],
                      RmsDbfs=db(row["Rms"]), PeakDbfs=db(row["Peak"]), BankDb=row["BankDb"],
                      IntegratedLufs=row["IntegratedLufs"], CrestDb=db(row["Peak"] / row["Rms"]),
                      OriginalAfterBankLufs=row["IntegratedLufs"] + row["BankDb"],
                      ShortTermP10=row["ShortTermP10"], ShortTermP95=row["ShortTermP95"])
        for target in sorted(set([-30, -24, -20, -16, -14, -12, args.target])):
            correction, reason = bounded_gain(target - result["RmsDbfs"] - 12 - row["BankDb"], row["Peak"], row["BankDb"])
            prefix = f"Target{target:g}"
            result[prefix + "CorrectionDb"] = correction
            result[prefix + "Limit"] = reason
            result[prefix + "RmsShortfallDb"] = max(0, target - 12 - (result["RmsDbfs"] + correction + row["BankDb"]))
            result[prefix + "PredictedLufs"] = row["IntegratedLufs"] + correction + row["BankDb"]
        summaries.append(result)
    prefix = f"Target{args.target:g}"
    predicted = [r[prefix + "PredictedLufs"] for r in summaries]
    lufs_target = statistics.median(predicted)
    alternative = []
    for row, result in zip(good, summaries):
        correction, reason = bounded_gain(lufs_target - row["IntegratedLufs"] - row["BankDb"], row["Peak"], row["BankDb"])
        result["LufsComparisonTarget"] = lufs_target
        result["LufsComparisonCorrectionDb"] = correction
        result["LufsComparisonLimit"] = reason
        result["LufsComparisonPredictedLufs"] = row["IntegratedLufs"] + row["BankDb"] + correction
        alternative.append(result["LufsComparisonPredictedLufs"])
    with (args.directory / "songs.csv").open("w", newline="", encoding="utf-8-sig") as output:
        writer = csv.DictWriter(output, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)
    (args.directory / "failures.json").write_text(json.dumps(failed + list(archive_errors.values()), indent=2), encoding="utf-8")
    duplicates = {key: count for key, count in collections.Counter(r["SongKey"].lower() for r in summaries).items() if count > 1}
    original = [r["OriginalAfterBankLufs"] for r in summaries]
    bank = [r["BankDb"] for r in summaries]
    def stats(values):
        return {"min": min(values), "p05": percentile(values, .05), "median": statistics.median(values),
                "p95": percentile(values, .95), "max": max(values),
                "p05_p95_span": percentile(values, .95) - percentile(values, .05)}
    summary = dict(successes=len(good), failures=len(failed), archive_errors=len(archive_errors),
                   non_song_archives=list(non_song_archives),
                   positive_banks=sum(x > 0 for x in bank), bank_stats=stats(bank), target=args.target,
                   original_lufs=stats(original), normalized_lufs=stats(predicted),
                   normalization_limited=sum(r[prefix + "Limit"] != "none" for r in summaries),
                   shortfall_over_1db=sum(r[prefix + "RmsShortfallDb"] > 1 for r in summaries),
                   lufs_comparison_target=lufs_target, lufs_comparison=stats(alternative),
                   lufs_comparison_limited=sum(r["LufsComparisonLimit"] != "none" for r in summaries),
                   duplicate_song_keys=duplicates)
    (args.directory / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    def safe(text):
        return str(text).replace("|", "/").replace("\n", " ")
    progress = (args.directory / "progress.txt").read_text() if (args.directory / "progress.txt").exists() else ""
    lines = ["# Song library normalization audit", "", progress.strip(), "",
             f"Analyzed **{len(good)} song entries**; {len(failed)} song failures and {len(archive_errors)} archive errors.",
             f"Calculations assume target **{args.target:g} dBFS RMS**, Song boost **0**, the common -12 dB bank reference, and no pitch shifting.",
             "These predict the decoded backing track after authored bank gain, before game faders and output processing. They do not prove decoder-hook matching or include guitar audio.", "",
             "## Loudness distribution", "", "| Measurement | 5th percentile | Median | 95th percentile | Middle 90% span |",
             "|---|---:|---:|---:|---:|"]
    for name, values in [("Original, after bank", original), ("Current RMS normalization", predicted), ("LUFS comparison", alternative)]:
        s = stats(values)
        lines.append(f"| {name} | {s['p05']:.2f} | {s['median']:.2f} | {s['p95']:.2f} | {s['p05_p95_span']:.2f} dB |")
    lines += ["", f"Embedded bank volumes range from **{min(bank):.1f} to {max(bank):+.1f} dB**; {summary['positive_banks']} are positive.",
              f"Current normalization is peak/gain limited on **{summary['normalization_limited']}** tracks; **{summary['shortfall_over_1db']}** miss their RMS target by more than 1 dB.",
              f"The comparison uses one fixed LUFS-based correction per track toward {lufs_target:.2f} LUFS (the current result's median), with the same gain and sample-peak bounds. {summary['lufs_comparison_limited']} tracks encounter bounds.",
              "The LUFS audit implements BS.1770-style stereo K weighting, 400 ms blocks with 75% overlap, a -70 LUFS absolute gate, and a -10 LU relative gate. It has synthetic calibration tests; it is not a certified meter. True peaks and limiter behavior under Song boost are not measured here.",
              "Filter reference: [libebur128](https://github.com/jiixyj/libebur128/blob/master/ebur128/ebur128.c).", "",
              "## Interpretation", "",
              "RMS normalization substantially improves the ordinary track-to-track spread. A remaining difference in K-weighted loudness even on unbounded tracks supports testing a LUFS-based mode, while preserving one fixed correction per song.",
              "The hypothetical LUFS result is equal by construction for tracks that can reach the chosen target. This is a feasibility calculation, not proof that all tracks will sound equally loud or that a new runtime implementation has been validated.",
              "Peak-limited outliers cannot be fixed by changing the loudness metric alone. They require a lower target or controlled compression/limiting if more average volume is wanted. The existing Song boost limiter is not simulated in this audit.",
              "Keep the current runtime algorithm while evaluating a LUFS option with representative songs and the outliers listed below. Treat archive/decoder coverage fixes separately from loudness-algorithm changes.", "",
              "## Target sensitivity", "", "| Target RMS | Limited tracks | More than 1 dB below target | Predicted LUFS middle 90% span |", "|---|---:|---:|---:|"]
    for target in [-30, -24, -20, -16, -14, -12]:
        p = f"Target{target}"
        values = [r[p + "PredictedLufs"] for r in summaries]
        lines.append(f"| {target} | {sum(r[p + 'Limit'] != 'none' for r in summaries)} | {sum(r[p + 'RmsShortfallDb'] > 1 for r in summaries)} | {stats(values)['p05_p95_span']:.2f} dB |")
    for title, rows in [("Loudest predictions after current normalization", sorted(summaries, key=lambda r: r[prefix + "PredictedLufs"], reverse=True)[:10]),
                        ("Quietest predictions after current normalization", sorted(summaries, key=lambda r: r[prefix + "PredictedLufs"])[:10])]:
        lines += ["", "## " + title, "", "| Artist | Song | Predicted LUFS | Correction | Bank | Limit |", "|---|---|---:|---:|---:|---|"]
        for row in rows:
            lines.append(f"| {safe(row['Artist'])} | {safe(row['Title'] or row['SongKey'])} | {row[prefix + 'PredictedLufs']:.2f} | {row[prefix + 'CorrectionDb']:+.2f} | {row['BankDb']:+.1f} | {row[prefix + 'Limit']} |")
    lines += ["", "## Coverage notes", "", f"{len(duplicates)} song keys occur in more than one archive. Each source is reported separately; the runtime uses its first matching archive.",
              "Menu preview banks are excluded. Unsupported audio, unreadable banks, and decoder failures are listed in failures.json. The scan warms the production RMS cache; extra audit metrics are stored only in this report directory."]
    if non_song_archives:
        lines += ["", "Readable archives containing no audio or soundbanks (not analysis failures):"]
        lines += ["- " + Path(path).name for path in non_song_archives]
    if archive_errors:
        lines += ["", "Archives not analyzed; these remain coverage gaps:"]
        lines += ["- " + Path(path).name + ": " + row["Error"].splitlines()[0] for path, row in archive_errors.items()]
    if failed:
        lines += ["", "## Analysis failures", "", "| Artist | Song | Error |", "|---|---|---|"]
        for row in failed:
            lines.append(f"| {safe(row.get('Artist', ''))} | {safe(row.get('Title') or row.get('SongKey', row.get('Bank', '')))} | {safe(row.get('Error', '').splitlines()[0])} |")
    (args.directory / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
