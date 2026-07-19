/*
 * usage_display — per-half Claude usage rendering.
 * Central/left: "C<five>" over "<HH:MM>" (5-hour window + its reset time).
 * Peripheral/right: "W<week>" (7-day window), delivered via the disp_us
 * global behavior relay. Values arrive through zmk_usage_set() from either
 * the transports (central) or the relayed behavior (peripheral).
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <lvgl.h>
#include <zmk/display.h>

static lv_obj_t *label;
static atomic_t v_five = ATOMIC_INIT(-1);
static atomic_t v_week = ATOMIC_INIT(-1);
static atomic_t v_hh = ATOMIC_INIT(-1);
static atomic_t v_mm = ATOMIC_INIT(-1);

static void update_cb(struct k_work *work) {
    if (label == NULL) {
        return;
    }
    char text[16];
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    int five = (int)atomic_get(&v_five);
    int hh = (int)atomic_get(&v_hh);
    int mm = (int)atomic_get(&v_mm);
    if (five >= 0 && hh >= 0) {
        snprintf(text, sizeof(text), "C%d %02d:%02d", five, hh, mm);
    } else if (five >= 0) {
        snprintf(text, sizeof(text), "C%d", five);
    } else {
        snprintf(text, sizeof(text), "C--");
    }
#else
    int week = (int)atomic_get(&v_week);
    if (week >= 0) {
        snprintf(text, sizeof(text), "W%d", week);
    } else {
        snprintf(text, sizeof(text), "W--");
    }
#endif
    lv_label_set_text(label, text);
}
static K_WORK_DEFINE(update_work, update_cb);

void zmk_usage_set(int five, int week, int hh, int mm) {
    if (five >= 0 && five <= 100) {
        atomic_set(&v_five, five);
    }
    if (week >= 0 && week <= 100) {
        atomic_set(&v_week, week);
    }
    if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
        atomic_set(&v_hh, hh);
        atomic_set(&v_mm, mm);
    }
    k_work_submit_to_queue(zmk_display_work_q(), &update_work);
}

lv_obj_t *zmk_usage_display_create(lv_obj_t *parent) {
    label = lv_label_create(parent);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    lv_label_set_text(label, "C--");
#else
    lv_label_set_text(label, "W--");
#endif
    return label;
}
