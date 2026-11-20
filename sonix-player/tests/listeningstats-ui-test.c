#include <assert.h>
#include <math.h>
#include <time.h>
static time_t test_now(time_t *out) {
	time_t t = 1795176000;
	if (out)
		*out = t;
	return t;
}
#define time test_now
#include "src/gui/library/statspage.c"
#undef time
#include "src/gui/shell/main_menu.h"
#include "src/gui/shell/morepage.h"
static int test_stage;
static char test_paths[4][512];
static char test_db[1024];
void __real_gui_init(gui_config_t *cfg);
static time_t date(int year, int month, int day) {
	struct tm d = {
	    .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = 12, .tm_isdst = -1};
	return mktime(&d);
}
static void check_period(stats_period_t p, double seconds, long long plays, int buckets) {
	listening_stats_t *s = malloc(sizeof(*s));
	assert(s);
	library_stats_read(p, date(2026, 11, 20), s);
	printf("CHECK period=%d seconds=%.1f plays=%lld buckets=%d from=%s until=%s\n", p, s->seconds, s->plays,
	       s->bucket_count, s->from, s->until);
	fflush(stdout);
	assert(s->available && fabs(s->seconds - seconds) < .01 && s->plays == plays &&
	       s->bucket_count == buckets);
	if (p == STATS_WEEK) {
		assert(!strcmp(s->from, "2026-11-16") && !strcmp(s->until, "2026-11-23"));
		assert(s->counts[STATS_ALBUMS] == 2);
		assert(strcmp(s->ranks[STATS_ALBUMS][0].value, s->ranks[STATS_ALBUMS][1].value));
	}
	free(s);
}
static lv_obj_t *test_find_label(lv_obj_t *obj, const char *value) {
	if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), value))
		return obj;
	for (unsigned i = 0; i < lv_obj_get_child_count(obj); i++) {
		lv_obj_t *found = test_find_label(lv_obj_get_child(obj, i), value);
		if (found)
			return found;
	}
	return NULL;
}
static void test_tick(lv_timer_t *timer) {
	(void)timer;
	switch (test_stage++) {
	case 0: {
		if (library_scan_running()) {
			test_stage--;
			break;
		}
		if (library_track_count() == 0) {
			assert(library_scan_start(getenv("ORIGINAL_CARD")));
			test_stage--;
			break;
		}
		snprintf(test_db, sizeof(test_db), "%s/.local/library.db", getenv("SONIX_SD_ROOT"));
		for (int i = 0; i < 4; i++)
			snprintf(test_paths[i], sizeof(test_paths[i]), "%s/album-%02d/01.flac", getenv("ORIGINAL_CARD"),
			         i);
		for (int i = 0; i < 12; i++)
			library_note_play(test_paths[0]);
		for (int i = 0; i < 3; i++)
			library_note_play(test_paths[1]);
		library_note_play(test_paths[3]);
		unsigned s = library_stats_session();
		library_stats_record(s, test_paths[0], "2026-11-20", 600, 2);
		library_stats_record(s, test_paths[1], "2026-11-18", 900, 3);
		library_stats_record(s, test_paths[2], "2026-11-01", 1200, 4);
		library_stats_record(s, test_paths[3], "2026-01-10", 1800, 5);
		library_stats_record(s, test_paths[0], "", 60, 1);
		check_period(STATS_WEEK, 1500, 5, 7);
		check_period(STATS_MONTH, 2700, 9, 30);
		check_period(STATS_YEAR, 4500, 14, 12);
		check_period(STATS_ALL, 4560, 16, 7);
		listening_stats_t *snap = malloc(sizeof(*snap));
		assert(snap);
		library_stats_read(STATS_MONTH, date(2024, 2, 29), snap);
		assert(snap->bucket_count == 29 && !strcmp(snap->until, "2024-03-01"));
		library_stats_read(STATS_WEEK, date(2026, 1, 1), snap);
		assert(!strcmp(snap->from, "2025-12-29") && !strcmp(snap->until, "2026-01-05"));
		library_stats_read(STATS_WEEK, date(2026, 3, 29), snap);
		assert(!strcmp(snap->from, "2026-03-23") && !strcmp(snap->until, "2026-03-30"));
		free(snap);
		library_close();
		library_stats_record(s, test_paths[0], "2026-11-20", 999, 99);
		assert(library_open(test_db));
		library_stats_record(s, test_paths[0], "2026-11-20", 999, 99);
		check_period(STATS_WEEK, 1500, 5, 7);
		assert(test_find_label(main_menu_screen, tr("stats")) &&
		       !test_find_label(main_menu_screen, tr("streaming")));
		assert(test_find_label(morepage_screen, tr("streaming")) &&
		       test_find_label(morepage_screen, tr("metronome")));
		puts("PASS: week/month/year/all totals, legacy counts, album identities, leap year, year boundary, "
		     "DST, reopen persistence and stale SD writes");
		switch_screen(statspage_screen);
		puts("SHOT stats");
		break;
	}
	case 1:
		assert(data && data->available && !strcmp(lv_label_get_text(plays_label), "5"));
		assert(!lv_obj_is_hidden(rows[0].box));
		assert(data->counts[STATS_TRACKS] == 2);
		assert(rows[0].cache >= 0 && art_cache[rows[0].cache].art.pixels);
		int cached = rows[0].cache;
		uint8_t *pixels = art_cache[cached].art.pixels;
		assert(lv_obj_get_style_image_opa(rows[0].image, 0) == LV_OPA_COVER);
		unsigned refs = art_cache[cached].refs;
		bind_row(10, STATS_ARTISTS, 0, bodies[0], section_rows[0]);
		assert(!rows[10].waiting && rows[10].cache == cached && art_cache[cached].art.pixels == pixels);
		assert(art_cache[cached].refs == refs + 1);
		assert(lv_obj_get_style_image_opa(rows[10].image, 0) == LV_OPA_TRANSP);
		release_row(10);
		lv_obj_set_hidden(rows[10].box, true);
		assert(art_cache[cached].refs == refs && art_cache[cached].art.pixels == pixels);
		puts("PASS: shared decoded cover cache, immediate reuse, pinned references and fade from zero "
		     "opacity");
		lv_obj_update_layout(statspage_screen);
		assert(lv_obj_is_hidden(note));
		char formatted[32];
		format_day("2026-11-20", 0, formatted, sizeof(formatted));
		assert(!strcmp(formatted, "Fri 20 Nov"));
		assert(!strcmp(lv_label_get_text(ranges[0]), "Mon 16 Nov – Sun 22 Nov"));
		for (int i = 0; i < 4; i++) {
			assert(lv_obj_get_height(period_buttons[0][i]) == 62);
			if (i)
				assert(lv_obj_get_x(period_buttons[0][i]) - lv_obj_get_x(period_buttons[0][i - 1]) -
				           lv_obj_get_width(period_buttons[0][i - 1]) ==
				       14);
		}
		lv_area_t link;
		lv_obj_get_coords(section_links[0], &link);
		assert(link.x2 == (int)lv_display_get_horizontal_resolution(NULL) - edge_padding - 1);
		assert(lv_obj_get_style_text_align(lv_obj_get_child(section_links[0], 0), 0) == LV_TEXT_ALIGN_RIGHT);
		assert(lv_obj_get_x(rows[0].rank) == 16);
		assert(lv_obj_get_x(rows[0].plays) + lv_obj_get_width(rows[0].plays) ==
		       lv_obj_get_width(rows[0].box) - 16);
		list_kind = STATS_ALBUMS;
		lv_label_set_text(list_title, tr(kind_tags[list_kind]));
		switch_screen(statslist_screen);
		puts("SHOT albums");
		break;
	case 2:
		assert(rows[0].kind == STATS_ALBUMS && rows[0].index == 0);
		// Recycle a fixed pool over a ranked list, rather than allocating a row per item.
		data->counts[STATS_ALBUMS] = LISTENING_STATS_TOP;
		for (int i = 2; i < LISTENING_STATS_TOP; i++)
			data->ranks[STATS_ALBUMS][i] = data->ranks[STATS_ALBUMS][0];
		paint();
		lv_obj_scroll_to_y(bodies[1], 2000, LV_ANIM_OFF);
		paint_rows();
		assert(rows[0].index > 10);
		period = STATS_ALL;
		request();
		break;
	case 3:
		assert(data && data->period == STATS_ALL && data->plays == 16);
		switch_screen(main_menu_screen);
		puts("SHOT home");
		break;
	case 4:
		switch_screen(morepage_screen);
		puts("SHOT more");
		break;
	case 5: {
		lv_obj_t *grid = lv_obj_get_child(morepage_screen, 0);
		lv_obj_update_layout(grid);
		assert(lv_obj_get_scroll_bottom(grid) > 0);
		lv_obj_scroll_to_y(grid, 9999, LV_ANIM_OFF);
		assert(lv_obj_get_scroll_y(grid) > 0);
		puts("SHOT more-bottom");
		break;
	}
	case 6:
		library_close();
		switch_screen(statspage_screen);
		break;
	case 7:
		assert(data && !data->available);
		assert(library_open(test_db));
		request();
		break;
	case 8:
		assert(data && data->available && data->plays == 16);
		puts("PASS: background UI refresh, thumbnails, ranked-list row recycling, period changes, More "
		     "scrolling, SD removal/reinsertion");
		assert(library_scan_start(getenv("ORIGINAL_CARD")));
		break;
	case 9:
		if (library_scan_running()) {
			test_stage--;
			break;
		}
		assert(library_scan_found() > 0);
		check_period(STATS_WEEK, 1500, 5, 7);
		check_period(STATS_ALL, 4560, 16, 7);
		puts("PASS: full library rescan preserves listening records and lifetime play counts");
		break;
	case 10:
		assert(data && !busy);
		for (int k = 0; k < STATS_KINDS; k++)
			data->counts[k] = 1;
		snprintf(data->ranks[STATS_ARTISTS][0].title, sizeof(data->ranks[STATS_ARTISTS][0].title),
		         "A particularly long artist name that should always end with dots");
		snprintf(data->ranks[STATS_ALBUMS][0].title, sizeof(data->ranks[STATS_ALBUMS][0].title),
		         "A particularly long album name that should always end with dots");
		snprintf(data->ranks[STATS_TRACKS][0].title, sizeof(data->ranks[STATS_TRACKS][0].title),
		         "A particularly long track name that should always end with dots");
		paint();
		lv_obj_update_layout(statspage_screen);
		for (int k = 0; k < STATS_KINDS; k++) {
			assert(lv_obj_get_height(rows[k * 3].title) == lv_font_get_line_height(&font_ui_22_bold));
			assert(lv_label_get_long_mode(rows[k * 3].title) == LV_LABEL_LONG_DOT);
			if (k)
				assert(lv_obj_get_y(section_titles[k]) == section_rows[k - 1] + ROW_H + SECTION_GAP);
		}
		puts("SHOT stats-single");
		break;
	case 11: {
		lv_obj_scroll_to_y(bodies[0], 9999, LV_ANIM_OFF);
		lv_obj_update_layout(bodies[0]);
		lv_area_t body, last;
		lv_obj_get_coords(bodies[0], &body);
		lv_obj_get_coords(rows[6].box, &last);
		assert(body.y2 - last.y2 == edge_padding);
		puts("SHOT stats-bottom");
		break;
	}
	case 12:
		puts("PASS: taller spaced period buttons, readable dates, right-aligned links, symmetric row "
		     "padding, single-line ellipses, compact sections and bottom margin");
		// Switch during an animated scroll to a shorter period. The old bar
		// must be hidden immediately, with no animation carrying its position on.
		lv_obj_scroll_to_y(bodies[0], 500, LV_ANIM_ON);
		lv_obj_send_event(period_buttons[0][STATS_WEEK], LV_EVENT_CLICKED, NULL);
		assert(period == STATS_WEEK && changing_period);
		assert(lv_obj_get_scrollbar_mode(bodies[0]) == LV_SCROLLBAR_MODE_OFF);
		assert(lv_obj_get_scroll_y(bodies[0]) == 0);
		assert(!lv_obj_is_scrolling(bodies[0]));
		puts("SHOT stats-tab-loading");
		break;
	case 13:
		assert(data && data->period == STATS_WEEK && !changing_period);
		assert(lv_obj_get_scrollbar_mode(bodies[0]) == LV_SCROLLBAR_MODE_AUTO);
		assert(lv_obj_get_scroll_y(bodies[0]) == 0);
		assert(rows[0].cache >= 0 && !rows[0].waiting);
		puts("SHOT stats-tab-reloaded");
		break;
	case 14: {
		// Saturate the LRU with missing-art entries; displayed covers stay pinned.
		int pinned = rows[0].cache;
		uint8_t *pixels = art_cache[pinned].art.pixels;
		for (int i = 0; i < ART_CACHE_COUNT * 2; i++) {
			char path[64];
			snprintf(path, sizeof(path), "/fake-cover-%d", i);
			cover_image_t blank = {0};
			assert(art_store(path, &blank) >= 0);
		}
		assert(art_cache[pinned].art.pixels == pixels && art_cache[pinned].refs > 0);
		library_close();
		assert(art_sync());
		for (int i = 0; i < ART_CACHE_COUNT; i++)
			assert(!art_cache[i].path[0] && !art_cache[i].art.pixels && !art_cache[i].refs);
		assert(library_open(test_db));
		request();
		break;
	}
	case 15:
		assert(data && data->available);
		lv_obj_send_event(period_buttons[0][STATS_MONTH], LV_EVENT_CLICKED, NULL);
		break;
	case 16:
		assert(data && data->period == STATS_MONTH && !changing_period);
		assert(lv_obj_get_scrollbar_mode(bodies[0]) == LV_SCROLLBAR_MODE_AUTO &&
		       lv_obj_get_scroll_y(bodies[0]) == 0);
		lv_obj_send_event(period_buttons[0][STATS_YEAR], LV_EVENT_CLICKED, NULL);
		break;
	case 17:
		assert(data && data->period == STATS_YEAR && !changing_period);
		assert(lv_obj_get_scrollbar_mode(bodies[0]) == LV_SCROLLBAR_MODE_AUTO &&
		       lv_obj_get_scroll_y(bodies[0]) == 0);
		lv_obj_send_event(period_buttons[0][STATS_WEEK], LV_EVENT_CLICKED, NULL);
		break;
	case 18:
		assert(data && data->period == STATS_WEEK && !changing_period);
		assert(lv_obj_get_scrollbar_mode(bodies[0]) == LV_SCROLLBAR_MODE_AUTO &&
		       lv_obj_get_scroll_y(bodies[0]) == 0);
		puts("PASS: Week/Month/Year/All transitions stop scrolling and rebuild bars; cover cache reuses "
		     "images, protects pinned entries and clears on SD changes");
		fflush(stdout);
		exit(0);
	}
	fflush(stdout);
}
void __wrap_gui_init(gui_config_t *cfg) {
	__real_gui_init(cfg);
	lv_timer_create(test_tick, 1800, NULL);
}
