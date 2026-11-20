#ifndef STATSPAGE_H
#define STATSPAGE_H
#include "lvgl/lvgl.h"
#include "src/gui/shell/gui.h"
extern lv_obj_t *statspage_screen;
extern lv_obj_t *statslist_screen;
void statspage_init(gui_config_t *cfg);
#endif
