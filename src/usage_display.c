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
static lv_obj_t *cost_label; /* central only */
static atomic_t v_five = ATOMIC_INIT(-1);
static atomic_t v_week = ATOMIC_INIT(-1);
static atomic_t v_hh = ATOMIC_INIT(-1);
static atomic_t v_mm = ATOMIC_INIT(-1);
static atomic_t v_o = ATOMIC_INIT(-1); /* $ today per model (int dollars) */
static atomic_t v_s = ATOMIC_INIT(-1);
static atomic_t v_f = ATOMIC_INIT(-1);

static void update_cb(struct k_work *work) {
    if (label == NULL) {
        return;
    }
    char text[20];
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
    lv_label_set_text(label, text);
    if (cost_label != NULL) {
        int o = (int)atomic_get(&v_o), sm = (int)atomic_get(&v_s);
        if (o >= 0 || sm >= 0) {
            snprintf(text, sizeof(text), "O%d S%d", o < 0 ? 0 : o, sm < 0 ? 0 : sm);
            lv_label_set_text(cost_label, text);
        } else {
            lv_label_set_text(cost_label, "");
        }
    }
#else
    int week = (int)atomic_get(&v_week);
    int f = (int)atomic_get(&v_f);
    if (week >= 0 && f >= 0) {
        snprintf(text, sizeof(text), "W%d F%d", week, f);
    } else if (f >= 0) {
        snprintf(text, sizeof(text), "F%d", f);
    } else if (week >= 0) {
        snprintf(text, sizeof(text), "W%d", week);
    } else {
        snprintf(text, sizeof(text), "W--");
    }
    lv_label_set_text(label, text);
#endif
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

void zmk_costs_set(int o, int s, int f) {
    if (o >= 0 && o <= 65535) {
        atomic_set(&v_o, o);
    }
    if (s >= 0 && s <= 65535) {
        atomic_set(&v_s, s);
    }
    if (f >= 0 && f <= 65535) {
        atomic_set(&v_f, f);
    }
    k_work_submit_to_queue(zmk_display_work_q(), &update_work);
}

/* central: dedicated $-line label; peripheral: merged into the main label */
lv_obj_t *zmk_costs_display_create(lv_obj_t *parent) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    cost_label = lv_label_create(parent);
    lv_label_set_text(cost_label, "");
    return cost_label;
#else
    return NULL;
#endif
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
