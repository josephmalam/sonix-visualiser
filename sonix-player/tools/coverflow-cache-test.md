# Cover Flow thumbnail cache test

Use a disposable music directory with the Linux simulator, or a spare microSD card on the R1. Include square JPEG and PNG covers, a non-square cover, an album with only embedded artwork, a track with both embedded artwork and a different `cover.jpg`, and an album with no artwork. Give two albums the same title and different album artists. Give one multi-disc album distinct embedded pictures on different tracks. Keep the source files or their hashes to compare after testing. Enable Cover Flow in Music settings and open it by swiping up from the bottom edge of the Music page.

1. Start the player with the fresh library and run a full scan. Leave Cover Flow closed. Wait for the `cover: album warmup` line. It should report one outcome per album. In the cache query below, a square cover has dimensions 210 by 210 and 88,200 bytes. Non-square images fit within the same box. A missing cover has a negative cache row.
2. Repeat the full scan and wait for the next summary. The same albums should all be reported as cached. Compare the cache row counts before and after this scan.
3. Enable Detect Changes. Add an album and run Detect Changes by taking the card out and returning it, or by restarting the simulator with that setting enabled. Only the new album should need preparation. Repeat without editing the card and check that the completed pass reports cache hits.
4. Replace a sidecar image in place while leaving the audio untouched. Run Detect Changes and check for the new sleeve. Remove the same sidecar, repeat, and check that the sleeve now uses the embedded image or the placeholder. Retag an embedded cover and verify the replacement on the next pass. The track selected for a multi-disc album should still be the same first disc and track used by Cover Flow. The player should still show each track's own artwork in Now Playing.
5. Scroll rapidly both ways while an initial pass is processing many albums. Check that artwork appears while the crate is moving, that it follows the correct album across slot rotations, and that the centre cover arrives before waiting side covers. Repeat while music is playing and listen for audio interruptions.
6. Start a fresh pass with enough albums to keep the worker busy. Remove the microSD on the R1 while it is working. Verify the player remains usable, then return the card. Previously committed rows should count as hits and unfinished covers should be prepared. Repeat with a cancelled scan and check that the cancelled scan does not start a new pass. Repeat with shutdown during a pass and check the cache on the next boot.
7. Reopen Cover Flow after the pass completes and verify each album uses its expected cover or placeholder. A cache hit loads RGB565 pixels directly. Repeat on a card that already has 210-pixel thumbnails from an earlier version to exercise reuse of the existing picture entries. Compare the original file hashes with the initial values.

For the simulator, set an isolated card and settings directory. Set `SONIX_COVERFLOW_TIMING` to log the mean and maximum request-to-display latency when closing Cover Flow.

```sh
SONIX_SD_ROOT=/path/to/test-card SONIX_CONFIG=/path/to/test.ini \
  SONIX_COVERFLOW_TIMING=1 ./sonix_player_host
```

Query the cache from the simulator host or after returning the card to a computer.

```sh
sqlite3 /path/to/test-card/.local/thumbnails \
  'SELECT w, h, length(pixels), count(*) FROM thumbs GROUP BY w, h, length(pixels);'
sqlite3 /path/to/test-card/.local/thumbnails 'PRAGMA quick_check;'
```
