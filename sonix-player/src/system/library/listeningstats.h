#ifndef LISTENINGSTATS_H
#define LISTENINGSTATS_H

#include <stdbool.h>
#include <time.h>

// Daily aggregates, rather than a row per second or an unbounded event log.
// Dates are local YYYY-MM-DD. An empty date preserves undated time for All.
#define LISTENING_STATS_TOP 50
#define LISTENING_STATS_BUCKETS 31
typedef enum { STATS_WEEK, STATS_MONTH, STATS_YEAR, STATS_ALL } stats_period_t;
typedef enum { STATS_ARTISTS, STATS_ALBUMS, STATS_TRACKS, STATS_KINDS } stats_kind_t;
typedef struct {
	char title[256], artist[256], value[768], path[512];
	long long plays;
	int tracks;
} stats_rank_t;
typedef struct {
	bool available;
	unsigned session;
	stats_period_t period;
	char from[11], until[11]; // inclusive start, exclusive end; empty for All
	double seconds;
	long long plays;
	int bucket_count;
	char bucket_keys[LISTENING_STATS_BUCKETS][11];
	double daily_seconds[LISTENING_STATS_BUCKETS];
	int counts[STATS_KINDS];
	stats_rank_t ranks[STATS_KINDS][LISTENING_STATS_TOP];
} listening_stats_t;

unsigned library_stats_session(void);
unsigned library_stats_revision(void);
// Rejects a write from a previous mount, including after swapping SD cards.
void library_stats_record(unsigned session, const char *path, const char *day, double seconds, int plays);
// Potentially large GROUP BY queries: call on a background thread. Fixed-size result.
void library_stats_read(stats_period_t period, time_t now, listening_stats_t *out);

#endif
