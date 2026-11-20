#include "playcount.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "listeningstats.h"
#include "src/system/device/clock.h"

#include "src/system/lastfm/lastfm.h"
#include "src/system/library/library.h"

// A longer step between two calls than this is the device having been asleep,
// not listening.
#define STEP_MAX_SECS 5.0

// Back at the start of the same file, after being this far into it: played
// again. The same test Last.fm's counting makes.
#define AGAIN_NEAR_START_SECS 5.0
#define AGAIN_BACK_SECS 10.0

// Playback is sampled on the GUI thread; storage teardown can flush it.
static pthread_mutex_t play_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	char file[512];
	bool skip;
	bool counted;
	bool was_playing;
	double listened;
	double last_tick;
	double last_position;
	double pending_seconds;
	int pending_plays;
	unsigned session;
	char day[11];
} play;

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void flush_locked(void) {
	if (!play.skip && (play.pending_seconds > 0 || play.pending_plays > 0))
		library_stats_record(play.session, play.file, play.day, play.pending_seconds, play.pending_plays);
	play.pending_seconds = 0;
	play.pending_plays = 0;
}
void playcount_flush(void) {
	pthread_mutex_lock(&play_lock);
	flush_locked();
	pthread_mutex_unlock(&play_lock);
}
static void note_playback(const playcount_playback_t *now) {
	double t = now_seconds();
	const char *file = now->file ? now->file : "";
	unsigned session = library_stats_session();
	bool same = play.file[0] && strcmp(play.file, file) == 0 && play.session == session;
	char day[11] = "";
	if (clock_is_set()) {
		time_t wall = time(NULL);
		struct tm date;
		localtime_r(&wall, &date);
		strftime(day, sizeof(day), "%Y-%m-%d", &date);
	}
	bool again =
	    same && now->position < AGAIN_NEAR_START_SECS && now->position + AGAIN_BACK_SECS < play.last_position;

	if (now->playing && file[0] && (!same || again)) {
		flush_locked();
		play.session = session;
		snprintf(play.day, sizeof(play.day), "%s", day);
		snprintf(play.file, sizeof(play.file), "%s", file);
		play.skip = now->skip;
		play.counted = false;
		play.was_playing = true;
		play.listened = 0;
		play.last_tick = t;
		play.last_position = now->position;
		return;
	}
	if (!same) {
		flush_locked();
		play.file[0] = '\0';
		return;
	}

	if (strcmp(day, play.day)) {
		flush_locked();
		snprintf(play.day, sizeof(play.day), "%s", day);
	}
	if (play.was_playing) {
		double step = t - play.last_tick;
		if (step > 0 && step < STEP_MAX_SECS) {
			play.listened += step;
			if (!play.skip)
				play.pending_seconds += step;
		}
	}
	play.was_playing = now->playing;
	play.last_tick = t;
	play.last_position = now->position;

	if (!play.counted && !play.skip && lastfm_play_counts(now->duration, play.listened)) {
		play.counted = true;
		library_note_play(play.file);
		play.pending_plays++;
	}
}

void playcount_note_playback(const playcount_playback_t *now) {
	pthread_mutex_lock(&play_lock);
	note_playback(now);
	if (play.pending_seconds >= 30 || play.pending_plays || !now->playing)
		flush_locked();
	pthread_mutex_unlock(&play_lock);
}
