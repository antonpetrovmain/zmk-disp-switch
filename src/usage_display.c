/*
 * usage_display — per-half Claude usage rendering.
 * Central/left: personal-subscription limits "C<5h%> <HH:MM>" (5h window +
 *   reset time) plus a small "cc" Claude Code badge.
 * Peripheral/right: work-account per-model spend "O$x.x S$x.x F$x.x" (dollars
 *   carried as integer TENTHS). On a limits-only keyboard the right instead
 *   shows "W<7d%>".
 * Values arrive via zmk_usage_set()/zmk_costs_set() from the transports
 * (central) or the relayed behaviors (peripheral).
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <lvgl.h>
#include <zmk/display.h>

static lv_obj_t *label;
static lv_obj_t *cost_label; /* central only: the "cc" badge */
static atomic_t v_five = ATOMIC_INIT(-1);
static atomic_t v_week = ATOMIC_INIT(-1);
static atomic_t v_hh = ATOMIC_INIT(-1);
static atomic_t v_mm = ATOMIC_INIT(-1);
static atomic_t v_o = ATOMIC_INIT(-1); /* $ today per model, in TENTHS ($4.7 => 47) */
static atomic_t v_s = ATOMIC_INIT(-1);
static atomic_t v_f = ATOMIC_INIT(-1);

static void update_cb(struct k_work *work) {
    if (label == NULL) {
        return;
    }
    char text[28];
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    int five = (int)atomic_get(&v_five);
    int hh = (int)atomic_get(&v_hh);
    int mm = (int)atomic_get(&v_mm);
    bool have_costs = atomic_get(&v_o) >= 0 || atomic_get(&v_s) >= 0 || atomic_get(&v_f) >= 0;
    if (five >= 0 && hh >= 0) {
        snprintf(text, sizeof(text), "C%02d %02d:%02d", five, hh, mm);
    } else if (five >= 0) {
        snprintf(text, sizeof(text), "C%02d", five);
    } else if (have_costs) {
        /* costs-only keyboard: no limits feed -> no C-- placeholder */
        text[0] = '\0';
    } else {
        snprintf(text, sizeof(text), "C--");
    }
    lv_label_set_text(label, text);
    if (cost_label != NULL) {
        /* small Claude Code badge — shown whenever this half has any CC feed */
        lv_label_set_text(cost_label, (five >= 0 || have_costs) ? "cc" : "");
    }
#else
    int week = (int)atomic_get(&v_week);
    int o = (int)atomic_get(&v_o), s = (int)atomic_get(&v_s), f = (int)atomic_get(&v_f);
    if (o >= 0 || s >= 0 || f >= 0) {
        /* no per-value '$' — 8px/char leaves only ~16 chars, and O$x.x S$x.x
         * F$x.x overflows (F clipped). Decimal kept; the left "cc" signals $. */
        int O = o < 0 ? 0 : o, S = s < 0 ? 0 : s, F = f < 0 ? 0 : f;
        snprintf(text, sizeof(text), "O%d.%d S%d.%d F%d.%d",
                 O / 10, O % 10, S / 10, S % 10, F / 10, F % 10);
    } else if (week >= 0) {
        snprintf(text, sizeof(text), "W%02d", week);
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

/* dollars carried as integer tenths (47 = $4.7); valid range 0..65535 */
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

/* central: dedicated "cc" badge label; peripheral: none (main label only) */
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
