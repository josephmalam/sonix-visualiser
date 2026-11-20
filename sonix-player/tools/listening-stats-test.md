# Listening statistics

Home's top-right tile opens Stats. Streaming is now the first tile in More;
More scrolls to retain all of its existing tools. The Stats tile's blocks face
the same way as Settings.

Stats offers calendar Week (Monday–Sunday), Month, Year and All. The overview
shows actual sampled listening time, qualifying plays, a daily/monthly chart,
and the first three artists, albums and tracks. See all opens the top 50 of
that category. Rankings use plays; ties sort by name. A fixed pool of twelve
rows and artwork slots serves ranked lists. Artist and album rows open their
library listings; a track opens its normal track row. Artwork uses the existing
background thumbnail worker and cache. A GUI-owned LRU retains up to 24 decoded
48px covers (roughly 120 KiB including keys/descriptors), shares them across rows,
and pins visible descriptors against eviction. New bindings fade from zero image
opacity over 200 ms; pending artwork stays invisible rather than flashing a
placeholder. Missing-art answers are cached too. Mount or library-generation
changes discard the RAM cache; idle entries older than a minute are revalidated
through the existing loader. Period changes stop scrolling, hide the scrollbar
while the content changes, then recalculate bounds and invalidate the page before
restoring it.

Only indexed local music counts, using the existing play qualification rule
(at least 30 seconds long, listened to for half its duration or four minutes,
whichever is shorter). Listening time includes partial listens, excludes pauses
and long suspend gaps, and measures elapsed playback time rather than seek
position. Separate artists linked by the library are credited independently;
artist totals therefore need not add up to the overall total.

The additive LISTENING_DAYS SQLite table stores one aggregate per track per
local date, including metadata snapshots and the existing album identity.
Rescans leave it and PLAY_COUNTS intact. Time is flushed every 30 seconds, at
qualification, pause, track changes and orderly storage detach/shutdown. An
unexpected power loss can lose up to about 30 seconds of time. A mount-session
guard prevents pending writes from reaching another card. If the clock is not
set, time is kept undated for All. Old lifetime play counts remain available
under All; dated counts and listening time start with this firmware. Historical
listening time cannot be reconstructed from the old counters.

A single temporary low-priority worker does the aggregation. Results are
published on the GUI thread and stale requests are discarded. While visible,
the page checks for changes periodically and avoids querying unchanged data.

## Automated checks

From `sonix-player/`, on macOS or Linux:

```sh
tools/test-listening-stats.sh
```

This deterministic sampler test checks qualification, repeat, pause, suspend,
seeking, excluded sources and card-session handling with no audio hardware.

For the integration test, use Linux with the documented host build dependencies
plus `python3`, `libsndfile1`, `xvfb` and `x11-utils`:

```sh
tools/test-listening-stats-ui.sh
```

The runner creates a disposable 24-album FLAC/PNG library, scans it with the real
player, builds the test against the host objects and uses Xvfb with dummy audio.
It checks period totals, legacy counts, same-named albums with different keys,
leap year/year rollover/DST, persistence, stale writes, asynchronous screens,
fixed row recycling, More scrolling, SD close/reopen, and a full rescan. It also checks taller spaced filter buttons, short date ranges, right-aligned links,
symmetric row padding, single-line truncation, variable section heights, and
bottom margins. Cache checks cover immediate reuse without a loader request,
shared references, fade start/end opacity, eviction protection and SD invalidation.
Tab checks exercise Week/Month/Year/All, including a change during animated scrolling.
It prints the directory containing screenshots and logs. It never uses a real SD
card or the user's settings. `SONIX_STATS_TEST_OUTPUT` optionally chooses a new,
empty output directory; the default is a fresh directory under `/tmp`.

## Physical R1 checks

1. Install the R1 image and open Home → Stats. Confirm that Streaming is under
   More and Metronome remains reachable by scrolling.
2. Check an existing heavily played track in All. Its earlier count should be
   retained; Week/Month/Year should not invent dates for those old plays.
3. Listen to local music, pause, and reopen Stats. Time should increase and a
   qualifying play should be counted once. Seeking forward should not add time.
4. Check artists, albums, tracks and their See all lists, then open an entry and
   return with Back. Change periods and theme/accent; check the chart and tabs.
5. Leave Stats open while playing and scroll the rankings. Confirm smooth audio
   and responsive navigation. Remove/reinsert the SD and check the empty/reload
   states. Rescan and reboot; statistics must remain.
6. Check a track crossing midnight and playback with an unset clock. Undated
   time belongs only to All; new valid dates belong to the proper local day.

The simulator does not establish physical SD-removal timing, device frame rate,
or real audio-output behaviour; these remain device acceptance checks.
