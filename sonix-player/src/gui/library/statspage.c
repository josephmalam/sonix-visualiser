#include "statspage.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h"
#include "medialist.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/nowplaying/coverloader.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/lang.h"
#include "src/system/core/utils.h"
#include "src/system/device/clock.h"
#include "src/system/library/library.h"
#include "src/system/library/listeningstats.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

lv_obj_t *statspage_screen, *statslist_screen;
#define STATS_ROWS 12
#define ART_CACHE_COUNT 24
#define ART_CACHE_TTL_MS 60000
#define ART_FADE_MS 200
#define ROW_H 80
#define ROW_PITCH 88
#define STATS_GAP 14
#define PERIOD_HEIGHT 62
#define RANGE_TOP (130 + PERIOD_HEIGHT + STATS_GAP)
#define BODY_TOP (RANGE_TOP + 22 + STATS_GAP)
#define SECTION_GAP 24
#define SECTION_HEADER_HEIGHT 44
static lv_obj_t *bodies[2], *ranges[2], *period_buttons[2][4];
static lv_obj_t *seconds_label, *plays_label, *chart, *chart_title, *note, *empty[2], *list_title,
    *section_empty[STATS_KINDS], *section_titles[STATS_KINDS], *section_links[STATS_KINDS];
static lv_obj_t *extents[2];
static int section_rows[STATS_KINDS], edge_padding;
static stats_period_t period = STATS_WEEK;
static stats_kind_t list_kind = STATS_ARTISTS;
static listening_stats_t *data;
static unsigned ticket, seen_revision;
static int seen_day;
static bool busy, dirty, changing_period;
static uint32_t last_refresh, session_checked, art_checked;
static const char *const period_tags[] = {"stats_week", "stats_month", "stats_year", "stats_all"};
static const char *const kind_tags[] = {"stats_top_artists", "stats_top_albums", "stats_top_tracks"};
static pthread_mutex_t result_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
	unsigned ticket;
	stats_period_t period;
	listening_stats_t result;
} stats_job_t;
static stats_job_t *completed;
typedef struct {
	lv_obj_t *box, *image, *placeholder, *rank, *title, *subtitle, *plays;
	int cache;
	int kind, index;
	bool waiting;
	char path[512];
} stats_row_t;
static stats_row_t rows[STATS_ROWS];
// GUI-owned LRU. Visible rows pin descriptors so eviction cannot invalidate a
// displayed image. One decoded 48px image can serve several Stats rows.
typedef struct {
	char path[512];
	cover_image_t art;
	unsigned refs, used;
	uint32_t loaded;
} stats_art_t;
static stats_art_t art_cache[ART_CACHE_COUNT];
static unsigned art_clock, art_session, art_revision;

static void art_opacity(void *obj, int32_t opacity) { lv_obj_set_style_image_opa(obj, (lv_opa_t)opacity, 0); }
static void art_fade(lv_obj_t *image) {
	lv_anim_delete(image, art_opacity);
	lv_obj_set_style_image_opa(image, LV_OPA_TRANSP, 0);
	lv_anim_t fade;
	lv_anim_init(&fade);
	lv_anim_set_var(&fade, image);
	lv_anim_set_exec_cb(&fade, art_opacity);
	lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
	lv_anim_set_duration(&fade, ART_FADE_MS);
	lv_anim_start(&fade);
}
static int art_find(const char *path) {
	for (int i = 0; i < ART_CACHE_COUNT; i++) {
		stats_art_t *a = &art_cache[i];
		if (a->path[0] && !strcmp(a->path, path) && (a->refs || lv_tick_elaps(a->loaded) < ART_CACHE_TTL_MS))
			return i;
	}
	return -1;
}
static int art_store(const char *path, cover_image_t *image) {
	int candidate = -1;
	for (int i = 0; i < ART_CACHE_COUNT; i++) {
		stats_art_t *a = &art_cache[i];
		if (a->refs)
			continue;
		if (candidate < 0 || !a->path[0] || a->used < art_cache[candidate].used)
			candidate = i;
		if (!a->path[0])
			break;
	}
	if (candidate < 0) {
		cover_free(image);
		return -1;
	}
	stats_art_t *a = &art_cache[candidate];
	if (a->art.pixels)
		lv_image_cache_drop(&a->art.dsc);
	cover_free(&a->art);
	snprintf(a->path, sizeof(a->path), "%s", path);
	a->art = *image;
	memset(image, 0, sizeof(*image));
	a->loaded = lv_tick_get();
	a->used = ++art_clock;
	return candidate;
}
static void art_show(stats_row_t *r, int index) {
	stats_art_t *a = &art_cache[index];
	a->refs++;
	a->used = ++art_clock;
	r->cache = index;
	r->waiting = false;
	if (a->art.pixels) {
		lv_image_set_src(r->image, &a->art.dsc);
		lv_obj_set_hidden(r->image, false);
		lv_obj_set_hidden(r->placeholder, true);
		art_fade(r->image);
	} else {
		lv_obj_set_hidden(r->image, true);
		lv_obj_set_hidden(r->placeholder, false);
		art_fade(r->placeholder);
	}
}
static bool visible(void) {
	return lv_screen_active() == statspage_screen || lv_screen_active() == statslist_screen;
}
static int page(void) { return lv_screen_active() == statslist_screen ? 1 : 0; }
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, const lv_font_t *font,
                       bool secondary) {
	lv_obj_t *obj = lv_label_create(parent);
	lv_obj_add_style(obj, secondary ? &theme_style_text_dim : &theme_style_text, 0);
	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_width(obj, w);
	lv_obj_set_height(obj, lv_font_get_line_height(font));
	lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
	lv_label_set_text(obj, text);
	lv_obj_set_clickable(obj, false);
	return obj;
}
static lv_obj_t *surface(lv_obj_t *parent, int x, int y, int w, int h) {
	lv_obj_t *obj = lv_obj_create(parent);
	lv_obj_add_style(obj, &theme_style_card, 0);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, w, h);
	lv_obj_set_style_radius(obj, 12, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_scrollable(obj, false);
	lv_obj_set_event_bubble(obj, true);
	return obj;
}
static void release_row(int i) {
	stats_row_t *r = &rows[i];
	coverloader_release(COVERLOADER_STATS_BASE + i);
	lv_anim_delete(r->image, art_opacity);
	lv_anim_delete(r->placeholder, art_opacity);
	lv_obj_set_style_image_opa(r->image, LV_OPA_TRANSP, 0);
	lv_obj_set_style_image_opa(r->placeholder, LV_OPA_TRANSP, 0);
	lv_image_set_src(r->image, NULL);
	if (r->cache >= 0 && art_cache[r->cache].refs)
		art_cache[r->cache].refs--;
	r->cache = -1;
	r->path[0] = 0;
	r->waiting = false;
	lv_obj_set_hidden(r->image, true);
	// Leave the artwork area blank until either a cover or a missing-art
	// answer is ready, so a placeholder does not flash before the fade.
	lv_obj_set_hidden(r->placeholder, true);
}
static bool art_sync(void) {
	unsigned session = library_stats_session(), revision = library_revision(LIBRARY_LIST_TRACKS);
	if (session == art_session && revision == art_revision)
		return false;
	for (int i = 0; i < STATS_ROWS; i++)
		release_row(i);
	for (int i = 0; i < ART_CACHE_COUNT; i++) {
		if (art_cache[i].art.pixels)
			lv_image_cache_drop(&art_cache[i].art.dsc);
		cover_free(&art_cache[i].art);
		memset(&art_cache[i], 0, sizeof(art_cache[i]));
	}
	art_session = session;
	art_revision = revision;
	art_clock = 0;
	return true;
}
static void row_clicked(lv_event_t *e) {
	if (player_sheet_drag_active() || switcher_back_drag_active())
		return;
	stats_row_t *r = lv_event_get_user_data(e);
	if (!data || r->index < 0 || r->index >= data->counts[r->kind])
		return;
	stats_rank_t *entry = &data->ranks[r->kind][r->index];
	if (r->kind == STATS_ARTISTS && entry->value[0])
		medialist_open_artist(entry->value);
	else if (r->kind == STATS_ALBUMS && entry->value[0])
		medialist_open(entry->title, LIBRARY_LIST_TRACKS, LIBRARY_FILTER_ALBUM, entry->value);
	else {
		const char *paths[] = {entry->path}, *names[] = {entry->title};
		medialist_open_paths(entry->title, paths, names, NULL, 1, NULL);
	}
}
static void bind_row(int slot, int kind, int index, lv_obj_t *parent, int y) {
	stats_row_t *r = &rows[slot];
	if (!data || index >= data->counts[kind]) {
		release_row(slot);
		lv_obj_set_hidden(r->box, true);
		return;
	}
	stats_rank_t *entry = &data->ranks[kind][index];
	r->kind = kind;
	r->index = index;
	if (lv_obj_get_parent(r->box) != parent)
		lv_obj_set_parent(r->box, parent);
	lv_obj_set_y(r->box, y);
	lv_obj_set_hidden(r->box, false);
	lv_label_set_text_fmt(r->rank, "%d", index + 1);
	lv_label_set_text(r->title, entry->title[0] ? entry->title : tr("stats_unknown"));
	if (kind == STATS_ARTISTS)
		lv_label_set_text_fmt(r->subtitle, tr(entry->tracks == 1 ? "stats_one_track" : "stats_tracks_count"),
		                      entry->tracks);
	else
		lv_label_set_text(r->subtitle, entry->artist);
	lv_label_set_text_fmt(r->plays, tr(entry->plays == 1 ? "stats_one_play" : "stats_plays_count"),
	                      entry->plays);
	if (strcmp(r->path, entry->path)) {
		release_row(slot);
		snprintf(r->path, sizeof(r->path), "%s", entry->path);
		if (r->path[0]) {
			int cached = art_find(r->path);
			if (cached >= 0)
				art_show(r, cached);
			else {
				coverloader_request(COVERLOADER_STATS_BASE + slot, r->path, 48);
				r->waiting = true;
			}
		}
	}
}
static void paint_rows(void) {
	if (!visible() || !data || data->period != period || changing_period)
		return;
	int p = page();
	if (p == 0) {
		for (int k = 0; k < STATS_KINDS; k++)
			for (int i = 0; i < 3; i++)
				bind_row(k * 3 + i, k, i, bodies[0], section_rows[k] + i * ROW_PITCH);
		for (int i = 9; i < STATS_ROWS; i++) {
			release_row(i);
			lv_obj_set_hidden(rows[i].box, true);
		}
	} else {
		int first = lv_obj_get_scroll_y(bodies[1]) / ROW_PITCH - 1;
		if (first < 0)
			first = 0;
		for (int i = 0; i < STATS_ROWS; i++)
			bind_row(i, list_kind, first + i, bodies[1], 8 + (first + i) * ROW_PITCH);
	}
}
static void chart_draw(lv_event_t *e) {
	if (!data || data->period != period || changing_period)
		return;
	lv_obj_t *obj = lv_event_get_target(e);
	lv_area_t area;
	lv_obj_get_coords(obj, &area);
	lv_layer_t *layer = lv_event_get_layer(e);
	int w = lv_obj_get_width(obj), h = lv_obj_get_height(obj), n = data->bucket_count;
	double maximum = 1;
	for (int i = 0; i < n; i++)
		if (data->daily_seconds[i] > maximum)
			maximum = data->daily_seconds[i];
	for (int i = 0; i < n; i++) {
		int x1 = area.x1 + i * w / n + 4, x2 = area.x1 + (i + 1) * w / n - 5;
		int baseline = area.y1 + h - 27;
		int plot_top = area.y1 + 8;
		int height = (int)((baseline - plot_top + 1) * data->daily_seconds[i] / maximum);
		if (height > 0) {
			lv_draw_rect_dsc_t d;
			lv_draw_rect_dsc_init(&d);
			d.bg_color = theme()->accent;
			d.radius = LV_MIN(5, (x2 - x1 + 1) / 2);
			lv_area_t bar = {x1, baseline - height + 1, x2, baseline};
			lv_draw_rect(layer, &d, &bar);
		}
		bool show = n <= 12 || i % 7 == 0;
		if (show) {
			char text[8];
			if (period == STATS_YEAR)
				snprintf(text, sizeof(text), "%d", i + 1);
			else if (period == STATS_MONTH)
				snprintf(text, sizeof(text), "%d", i + 1);
			else {
				struct tm d = {0};
				sscanf(data->bucket_keys[i], "%d-%d-%d", &d.tm_year, &d.tm_mon, &d.tm_mday);
				d.tm_year -= 1900;
				d.tm_mon--;
				d.tm_hour = 12;
				d.tm_isdst = -1;
				mktime(&d);
				strftime(text, sizeof(text), "%a", &d);
				text[1] = '\0';
			}
			lv_draw_label_dsc_t d;
			lv_draw_label_dsc_init(&d);
			d.color = theme()->text_secondary;
			d.font = &font_ui_16;
			d.text = text;
			d.text_local = true;
			d.align = LV_TEXT_ALIGN_CENTER;
			lv_area_t a = {area.x1 + i * w / n, area.y1 + h - 21, area.x1 + (i + 1) * w / n - 1, area.y2};
			lv_draw_label(layer, &d, &a);
		}
	}
}
static void format_day(const char *iso, int offset, char *out, size_t size) {
	struct tm date = {0};
	if (sscanf(iso, "%d-%d-%d", &date.tm_year, &date.tm_mon, &date.tm_mday) != 3) {
		out[0] = '\0';
		return;
	}
	date.tm_year -= 1900;
	date.tm_mon--;
	date.tm_mday += offset;
	date.tm_hour = 12;
	date.tm_isdst = -1;
	mktime(&date);
	strftime(out, size, "%a %-d %b", &date);
}

static void layout_sections(void) {
	// The chart ends at 322. Keep a clear gap before the first heading;
	// subsequent sections follow only the rows that actually exist.
	int y = 322 + SECTION_GAP;
	if (!lv_obj_is_hidden(note)) {
		lv_obj_set_y(note, y);
		lv_obj_update_layout(note);
		y += lv_obj_get_height(note) + SECTION_GAP;
	}
	for (int k = 0; k < STATS_KINDS; k++) {
		lv_obj_set_y(section_titles[k], y);
		lv_obj_set_y(section_links[k], y - 4);
		y += SECTION_HEADER_HEIGHT;
		section_rows[k] = y;
		lv_obj_set_y(section_empty[k], y);
		int count = LV_MIN(data->counts[k], 3);
		if (count)
			y += (count - 1) * ROW_PITCH + ROW_H;
		else
			y += lv_obj_get_height(section_empty[k]);
		if (k + 1 < STATS_KINDS)
			y += SECTION_GAP;
	}
	// A transparent extent keeps the same edge margin after the final card.
	lv_obj_set_y(extents[0], y + edge_padding - 1);
	int count = data->counts[list_kind];
	int end = count ? 8 + (count - 1) * ROW_PITCH + ROW_H : 20 + lv_obj_get_height(empty[1]);
	lv_obj_set_y(extents[1], end + edge_padding - 1);
}

static void paint(void) {
	if (!data || data->period != period)
		return;
	char range[80];
	if (period == STATS_ALL)
		snprintf(range, sizeof(range), "%s", tr("stats_all_time"));
	else {
		char from[32], until[32];
		format_day(data->from, 0, from, sizeof(from));
		format_day(data->until, -1, until, sizeof(until));
		snprintf(range, sizeof(range), "%s – %s", from, until);
	}
	for (int p = 0; p < 2; p++) {
		lv_label_set_text(ranges[p], range);
		for (int i = 0; i < 4; i++)
			lv_obj_set_style_bg_color(period_buttons[p][i],
			                          i == (int)period ? theme()->accent : theme()->panel, 0);
	}
	long long minutes = (long long)(data->seconds / 60);
	lv_label_set_text_fmt(seconds_label, "%lldh %02lldm", minutes / 60, minutes % 60);
	lv_label_set_text_fmt(plays_label, "%lld", data->plays);
	lv_label_set_text(chart_title, tr(period == STATS_ALL    ? "stats_last_seven_days"
	                                  : period == STATS_YEAR ? "stats_monthly_listening"
	                                                         : "stats_daily_listening"));
	lv_obj_invalidate(chart);
	bool notice = !data->available || !clock_is_set();
	lv_obj_set_hidden(note, !notice);
	lv_label_set_text(note, notice ? tr(!data->available ? "stats_no_card" : "stats_clock_note") : "");
	for (int p = 0; p < 2; p++) {
		bool none = p && (!data->available || data->counts[list_kind] == 0);
		lv_label_set_text(empty[p], tr(data->available ? "stats_empty" : "stats_no_card"));
		lv_obj_set_hidden(empty[p], !none);
	}
	art_sync();
	for (int k = 0; k < STATS_KINDS; k++) {
		lv_obj_set_hidden(section_titles[k], false);
		lv_obj_set_hidden(section_links[k], false);
	}
	layout_sections();
	for (int k = 0; k < STATS_KINDS; k++)
		lv_obj_set_hidden(section_empty[k], !data->available || data->counts[k] > 0);
	bool was_changing = changing_period;
	changing_period = false;
	paint_rows();
	// Recalculate the new content bounds before bringing the scrollbar back.
	// Full-page invalidation clears pixels left by the previous tab's bar.
	for (int p = 0; p < 2; p++) {
		lv_obj_update_layout(bodies[p]);
		lv_obj_readjust_scroll(bodies[p], LV_ANIM_OFF);
		lv_obj_set_scrollbar_mode(bodies[p], LV_SCROLLBAR_MODE_AUTO);
		if (was_changing)
			lv_obj_invalidate(p ? statslist_screen : statspage_screen);
	}
}
static void *worker(void *arg) {
	stats_job_t *job = arg;
	thread_be_low_priority("listening stats");
	library_stats_read(job->period, time(NULL), &job->result);
	pthread_mutex_lock(&result_lock);
	completed = job;
	pthread_mutex_unlock(&result_lock);
	return NULL;
}
static void request(void) {
	ticket++;
	dirty = true;
}
static void tick(lv_timer_t *timer) {
	(void)timer;
	if (visible() && lv_tick_elaps(art_checked) > 1000) {
		art_checked = lv_tick_get();
		if (art_sync())
			paint_rows();
	}
	pthread_mutex_lock(&result_lock);
	stats_job_t *job = completed;
	completed = NULL;
	pthread_mutex_unlock(&result_lock);
	if (job) {
		busy = false;
		if (visible() && job->ticket == ticket && job->result.session == library_stats_session()) {
			free(data);
			data = malloc(sizeof(*data));
			if (data) {
				memcpy(data, &job->result, sizeof(*data));
				paint();
			}
		} else if (visible())
			dirty = true;
		free(job);
	}
	if (!visible())
		return;
	if (lv_tick_elaps(session_checked) > 1000) {
		session_checked = lv_tick_get();
		if (data && data->session != library_stats_session()) {
			free(data);
			data = NULL;
			for (int i = 0; i < STATS_ROWS; i++) {
				release_row(i);
				lv_obj_set_hidden(rows[i].box, true);
			}
			request();
		}
	}
	if (lv_tick_elaps(last_refresh) > 15000) {
		last_refresh = lv_tick_get();
		time_t now = time(NULL);
		struct tm local;
		localtime_r(&now, &local);
		unsigned revision = library_stats_revision();
		int day = local.tm_year * 366 + local.tm_yday;
		if (revision != seen_revision || day != seen_day) {
			seen_revision = revision;
			seen_day = day;
			request();
		}
	}
	if (dirty && !busy) {
		stats_job_t *next = calloc(1, sizeof(*next));
		if (next) {
			next->ticket = ticket;
			next->period = period;
			pthread_t thread;
			if (pthread_create(&thread, NULL, worker, next) == 0) {
				pthread_detach(thread);
				busy = true;
				dirty = false;
			} else
				free(next);
		}
	}
	for (int i = 0; i < STATS_ROWS; i++)
		if (rows[i].waiting) {
			stats_row_t *r = &rows[i];
			int cached = art_find(r->path);
			if (cached >= 0) {
				coverloader_release(COVERLOADER_STATS_BASE + i);
				art_show(r, cached);
				continue;
			}
			bool finished = false;
			cover_image_t image = {0};
			coverloader_take(COVERLOADER_STATS_BASE + i, &image, &finished);
			if (finished) {
				int stored = art_store(r->path, &image);
				if (stored >= 0)
					art_show(r, stored);
				else
					r->waiting = false;
			}
		}
}
static void period_clicked(lv_event_t *e) {
	if (switcher_back_drag_active() || player_sheet_drag_active())
		return;
	stats_period_t selected = (stats_period_t)(intptr_t)lv_event_get_user_data(e);
	if (selected == period)
		return;
	changing_period = true;
	period = selected;
	for (int p = 0; p < 2; p++) {
		lv_obj_stop_scroll_anim(bodies[p]);
		lv_obj_set_scrollbar_mode(bodies[p], LV_SCROLLBAR_MODE_OFF);
		lv_label_set_text(ranges[p], tr("stats_loading"));
		lv_obj_scroll_to_y(bodies[p], 0, LV_ANIM_OFF);
		for (int i = 0; i < 4; i++)
			lv_obj_set_style_bg_color(period_buttons[p][i],
			                          i == (int)period ? theme()->accent : theme()->panel, 0);
	}
	for (int i = 0; i < STATS_ROWS; i++) {
		release_row(i);
		lv_obj_set_hidden(rows[i].box, true);
	}
	for (int k = 0; k < STATS_KINDS; k++) {
		lv_obj_set_hidden(section_titles[k], true);
		lv_obj_set_hidden(section_links[k], true);
		lv_obj_set_hidden(section_empty[k], true);
	}
	for (int p = 0; p < 2; p++) {
		lv_obj_set_y(extents[p], 0);
		lv_obj_update_layout(bodies[p]);
		lv_obj_invalidate(p ? statslist_screen : statspage_screen);
	}
	lv_obj_invalidate(chart);
	lv_label_set_text(empty[page()], tr("stats_loading"));
	lv_obj_set_hidden(empty[page()], false);
	request();
}
static void see_all(lv_event_t *e) {
	if (switcher_back_drag_active() || player_sheet_drag_active())
		return;
	list_kind = (stats_kind_t)(intptr_t)lv_event_get_user_data(e);
	lv_label_set_text(list_title, tr(kind_tags[list_kind]));
	switch_screen(statslist_screen);
}
static void loaded(lv_event_t *e) {
	(void)e;
	last_refresh = lv_tick_get();
	paint();
	request();
}
static void unloaded(lv_event_t *e) {
	(void)e;
	ticket++;
	for (int i = 0; i < STATS_ROWS; i++)
		release_row(i);
}
static void scrolled(lv_event_t *e) {
	(void)e;
	if (page() == 1)
		paint_rows();
}
static void refresh_theme(void) {
	if (visible())
		paint();
}

void statspage_init(gui_config_t *cfg) {
	edge_padding = cfg->padding;
	int width = cfg->screen_width - 2 * cfg->padding;
	lv_obj_t *screens[] = {statspage_screen, statslist_screen};
	for (int p = 0; p < 2; p++) {
		bodies[p] = settingsrow_page(screens[p], cfg, p ? "stats_top_artists" : "stats");
		lv_obj_set_layout(bodies[p], LV_LAYOUT_NONE);
		lv_obj_set_style_pad_all(bodies[p], 0, 0);
		lv_obj_set_y(bodies[p], BODY_TOP);
		lv_obj_set_height(bodies[p], cfg->screen_height - BODY_TOP);
		lv_obj_set_scroll_dir(bodies[p], LV_DIR_VER);
		lv_obj_set_scroll_elastic(bodies[p], false);
		player_sheet_attach_drag(bodies[p], true);
		lv_obj_add_event_cb(screens[p], loaded, LV_EVENT_SCREEN_LOADED, NULL);
		lv_obj_add_event_cb(screens[p], unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);
		for (int i = 0; i < 4; i++) {
			int bw = (width - 3 * STATS_GAP) / 4;
			lv_obj_t *button =
			    surface(screens[p], cfg->padding + i * (bw + STATS_GAP), 130, bw, PERIOD_HEIGHT);
			period_buttons[p][i] = button;
			lv_obj_set_style_bg_color(button, i == 0 ? theme()->accent : theme()->panel, 0);
			lv_obj_t *caption = label(button, tr(period_tags[i]), 0, 0, bw, &font_ui_20, false);
			lv_obj_center(caption);
			lv_obj_set_style_text_align(lv_obj_get_child(button, 0), LV_TEXT_ALIGN_CENTER, 0);
			lv_obj_add_event_cb(button, period_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
		}
		ranges[p] = label(screens[p], tr("stats_loading"), cfg->padding, RANGE_TOP, width, &font_ui_18, true);
		lv_obj_set_style_text_align(ranges[p], LV_TEXT_ALIGN_CENTER, 0);
		empty[p] =
		    label(bodies[p], tr("stats_loading"), cfg->padding, p ? 20 : 346, width, &font_ui_20, true);
		lv_obj_set_style_text_align(empty[p], LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_hidden(empty[p], p == 0);
	}
	list_title = settingsrow_page_title(statslist_screen);
	lv_obj_add_event_cb(bodies[1], scrolled, LV_EVENT_SCROLL, NULL);
	int half = (width - 14) / 2;
	lv_obj_t *a = surface(bodies[0], cfg->padding, 8, half, 110);
	label(a, tr("stats_listening_time"), 16, 12, half - 30, &font_ui_18, true);
	seconds_label = label(a, "0h 00m", 16, 43, half - 30, &font_ui_32, false);
	a = surface(bodies[0], cfg->padding + half + 14, 8, half, 110);
	label(a, tr("stats_tracks_played"), 16, 12, half - 30, &font_ui_18, true);
	plays_label = label(a, "0", 16, 43, half - 30, &font_ui_32, false);
	a = surface(bodies[0], cfg->padding, 132, width, 190);
	chart_title = label(a, tr("stats_daily_listening"), 16, 12, width - 32, &font_ui_22_bold, false);
	chart = lv_obj_create(a);
	lv_obj_remove_style_all(chart);
	lv_obj_set_pos(chart, 16, 48);
	lv_obj_set_size(chart, width - 32, 130);
	lv_obj_set_clickable(chart, false);
	lv_obj_add_event_cb(chart, chart_draw, LV_EVENT_DRAW_MAIN, NULL);
	note = label(bodies[0], "", cfg->padding, 346, width, &font_ui_16, true);
	lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
	lv_obj_set_height(note, LV_SIZE_CONTENT);
	lv_obj_set_hidden(note, true);
	for (int k = 0; k < STATS_KINDS; k++) {
		section_empty[k] =
		    label(bodies[0], tr("stats_empty"), cfg->padding, 420 + k * 320, width, &font_ui_20, true);
		lv_obj_set_hidden(section_empty[k], true);
		section_titles[k] =
		    label(bodies[0], tr(kind_tags[k]), cfg->padding, 0, width - 125, &font_ui_24_bold, false);
		a = surface(bodies[0], cfg->screen_width - cfg->padding - 110, 0, 110, 40);
		section_links[k] = a;
		lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
		lv_obj_set_ext_click_area(a, 5);
		lv_obj_t *t = label(a, tr("stats_see_all"), 0, 9, 110, &font_ui_18, false);
		lv_obj_set_style_text_color(t, theme()->accent, 0);
		lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_add_event_cb(a, see_all, LV_EVENT_CLICKED, (void *)(intptr_t)k);
	}
	for (int i = 0; i < STATS_ROWS; i++) {
		stats_row_t *r = &rows[i];
		r->index = -1;
		r->cache = -1;
		r->box = surface(bodies[0], cfg->padding, 0, width, ROW_H);
		lv_obj_add_style(r->box, &theme_style_card_pressed, LV_STATE_PRESSED);
		lv_obj_set_event_bubble(r->box, true);
		lv_obj_add_event_cb(r->box, row_clicked, LV_EVENT_CLICKED, r);
		r->rank = label(r->box, "", 16, 26, 25, &font_ui_18, true);
		r->image = lv_image_create(r->box);
		lv_obj_set_pos(r->image, 49, 16);
		lv_obj_set_size(r->image, 48, 48);
		lv_obj_set_clickable(r->image, false);
		r->placeholder = lv_image_create(r->box);
		lv_image_set_src(r->placeholder, &icon_music_note);
		lv_obj_set_pos(r->placeholder, 49, 16);
		lv_obj_set_size(r->placeholder, 48, 48);
		lv_obj_add_style(r->placeholder, &theme_style_icon, 0);
		lv_obj_set_clickable(r->placeholder, false);
		r->title = label(r->box, "", 111, 13, width - 229, &font_ui_22_bold, false);
		r->subtitle = label(r->box, "", 111, 43, width - 229, &font_ui_16, true);
		r->plays = label(r->box, "", width - 106, 29, 90, &font_ui_18, true);
		lv_obj_set_style_text_align(r->plays, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_set_hidden(r->box, true);
		lv_obj_set_hidden(r->image, true);
		lv_obj_set_hidden(r->placeholder, true);
	}
	for (int p = 0; p < 2; p++) {
		extents[p] = lv_obj_create(bodies[p]);
		lv_obj_remove_style_all(extents[p]);
		lv_obj_set_size(extents[p], 1, 1);
		lv_obj_set_clickable(extents[p], false);
	}
	theme_register_refresh(refresh_theme);
	lv_timer_create(tick, 75, NULL);
}
