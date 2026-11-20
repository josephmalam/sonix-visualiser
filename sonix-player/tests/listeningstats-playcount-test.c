// Deterministic sampling tests; compile/run with tools/test-listening-stats.sh.
#include <assert.h>
#include <stdio.h>
#include <time.h>
static double elapsed;
static int fake_gettime(clockid_t id, struct timespec *ts) {
	(void)id;
	ts->tv_sec = (time_t)elapsed;
	ts->tv_nsec = (long)((elapsed - ts->tv_sec) * 1e9);
	return 0;
}
#define clock_gettime fake_gettime
#include "src/system/library/playcount.c"
#undef clock_gettime
static unsigned mount = 7;
static double saved;
static int qualified, daily_plays;
unsigned library_stats_session(void) { return mount; }
bool clock_is_set(void) { return true; }
bool lastfm_play_counts(double duration, double listened) {
	return duration >= 30 && listened >= (duration / 2 < 240 ? duration / 2 : 240);
}
void library_note_play(const char *path) {
	(void)path;
	qualified++;
}
void library_stats_record(unsigned session, const char *path, const char *day, double seconds, int plays) {
	assert(path[0] && strlen(day) == 10);
	if (session == mount) {
		saved += seconds;
		daily_plays += plays;
	}
}
static void sample(bool playing, double position, bool skip, const char *file) {
	playcount_playback_t state = {playing, file, position, 60, skip};
	playcount_note_playback(&state);
}
int main(void) {
	sample(true, 0, false, "/music/a.flac");
	for (int i = 1; i <= 30; i++) {
		elapsed++;
		sample(true, i, false, "/music/a.flac");
	}
	assert(qualified == 1 && daily_plays == 1 && saved == 30);
	for (int i = 0; i < 10; i++) {
		elapsed++;
		sample(false, 30, false, "/music/a.flac");
	}
	assert(saved == 31); // one last interval until the pause was observed
	elapsed++;
	sample(true, 30, false, "/music/a.flac");
	elapsed += 60;
	sample(true, 31, false, "/music/a.flac");
	playcount_flush();
	assert(saved == 31);
	elapsed++;
	sample(true, 120, false, "/music/a.flac");
	playcount_flush();
	assert(saved == 32); // seek adds wall time only
	elapsed++;
	sample(true, 0, false, "/music/a.flac"); // repeat restarts qualification
	for (int i = 1; i <= 30; i++) {
		elapsed++;
		sample(true, i, false, "/music/a.flac");
	}
	assert(qualified == 2 && daily_plays == 2);
	double before = saved;
	elapsed++;
	sample(true, 0, true, "/podcast/a.flac");
	for (int i = 1; i <= 40; i++) {
		elapsed++;
		sample(true, i, true, "/podcast/a.flac");
	}
	playcount_flush();
	assert(saved == before && qualified == 2);
	elapsed++;
	sample(true, 0, false, "/music/a.flac");
	elapsed++;
	sample(true, 1, false, "/music/a.flac");
	mount++;
	playcount_flush();
	assert(saved == before); // pending time cannot leak onto another card
	puts("PASS: qualification, repeat, pause, suspend gap, seek, excluded sources and SD session guard");
	return 0;
}
