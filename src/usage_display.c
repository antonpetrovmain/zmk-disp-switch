/*
 * usage_display — per-half Claude usage rendering.
 * Central/left: personal-subscription limits "%<5h%> <HH:MM> W<7d%>".
 * Peripheral/right: work-account per-model spend "$<total> F<f> O<o> S<s>" in
 *   whole dollars (values carried as integer TENTHS, rounded for display; the
 *   leading bare number is the day's total). On a limits-only keyboard the
 *   right instead shows "W<7d%>".
 * Values arrive via zmk_usage_set()/zmk_costs_set() from the transports
 * (central) or the relayed behaviors (peripheral).
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
    int week = (int)atomic_get(&v_week);
    int hh = (int)atomic_get(&v_hh);
    int mm = (int)atomic_get(&v_mm);
    bool have_costs = atomic_get(&v_o) >= 0 || atomic_get(&v_s) >= 0 || atomic_get(&v_f) >= 0;
    /* near-cap warning = a leading "!" (NO colour styling — an explicit
     * text_color rendered white = OFF on this mono OLED and hid the label). */
    const char *w = (five >= CONFIG_ZMK_DISP_SW_LIMIT_WARN_PCT ||
                     week >= CONFIG_ZMK_DISP_SW_LIMIT_WARN_PCT) ? "!" : "";
    if (five >= 0 && week >= 0 && hh >= 0) {
        snprintf(text, sizeof(text), "%s%%%02d %02d:%02d W%02d", w, five, hh, mm, week);
    } else if (five >= 0 && hh >= 0) {
        snprintf(text, sizeof(text), "%s%%%02d %02d:%02d", w, five, hh, mm);
    } else if (five >= 0) {
        snprintf(text, sizeof(text), "%s%%%02d", w, five);
    } else if (have_costs) {
        /* costs-only keyboard: no limits feed -> no %-- placeholder */
        text[0] = '\0';
    } else {
        snprintf(text, sizeof(text), "%%--");
    }
    lv_label_set_text(label, text);
#else
    int week = (int)atomic_get(&v_week);
    int o = (int)atomic_get(&v_o), s = (int)atomic_get(&v_s), f = (int)atomic_get(&v_f);
    if (o >= 0 || s >= 0 || f >= 0) {
        /* whole dollars (tenths rounded), total first, then F O S:
         * "<total> F<f> O<o> S<s>". Integers make room for the total. */
        int Od = o < 0 ? 0 : (o + 5) / 10;
        int Sd = s < 0 ? 0 : (s + 5) / 10;
        int Fd = f < 0 ? 0 : (f + 5) / 10;
        snprintf(text, sizeof(text), "$%d F%d O%d S%d", Od + Sd + Fd, Fd, Od, Sd);
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

/* cc badge dropped — nothing to create; kept as a no-op so status_screen and
 * its weak-symbol wiring still link. */
lv_obj_t *zmk_costs_display_create(lv_obj_t *parent) {
    return NULL;
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
