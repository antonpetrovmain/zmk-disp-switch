/*
 * Custom status screen: the stock ZMK widgets (battery %, output/link, layer)
 * plus a battery-ETA label — estimated time to 0% assuming the screens-dark
 * drain model (CONFIG_ZMK_DISP_SW_BATT_MAH / CONFIG_ZMK_DISP_SW_DRAIN_UA).
 * Overrides ZMK's weak zmk_display_status_screen() when
 * CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y. Works on both halves; each half
 * shows its own battery's ETA.
 *
 * SPDX-License-Identifier: MIT
 */
#include <lvgl.h>
#include <zephyr/kernel.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
#include <zmk/display/widgets/battery_status.h>
static struct zmk_widget_battery_status battery_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
#include <zmk/display/widgets/output_status.h>
static struct zmk_widget_output_status output_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
#include <zmk/display/widgets/peripheral_status.h>
static struct zmk_widget_peripheral_status peripheral_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
#include <zmk/display/widgets/layer_status.h>
static struct zmk_widget_layer_status layer_status_widget;
#endif

/* ---- Claude usage label (strong impl in claude_usage.c when the
 * claude-uart snippet is applied; NULL stub otherwise) ---------------------- */

__attribute__((weak)) lv_obj_t *zmk_claude_usage_create(lv_obj_t *parent) { return NULL; }

/* ---- battery ETA label ---------------------------------------------------- */

static lv_obj_t *eta_label;
static lv_obj_t *cu_label_ref;

/* Adaptive layout, called by behavior_disp_switch on the display work queue:
 * the bottom-right slot is ETA on battery (charging ETA is meaningless) and
 * the Claude-usage % on USB (which only has data when cabled anyway). */
void disp_sw_layout_refresh(bool usb) {
    if (eta_label != NULL) {
        if (usb) {
            lv_obj_add_flag(eta_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(eta_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (cu_label_ref != NULL) {
        if (usb) {
            lv_obj_clear_flag(cu_label_ref, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(cu_label_ref, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

struct eta_state {
    uint8_t soc;
};

static struct eta_state eta_get_state(const zmk_event_t *eh) {
    return (struct eta_state){.soc = zmk_battery_state_of_charge()};
}

static void eta_update_cb(struct eta_state state) {
    if (eta_label == NULL) {
        return;
    }
    /* hours*10 = soc% * mAh * 100 / uA  (dark-screen drain model) */
    uint32_t h10 = ((uint32_t)state.soc * CONFIG_ZMK_DISP_SW_BATT_MAH * 100) /
                   CONFIG_ZMK_DISP_SW_DRAIN_UA;
    char text[12];
    uint32_t days = h10 / 240;
    uint32_t hours = (h10 % 240) / 10;
    if (days > 0) {
        snprintf(text, sizeof(text), "~%ud%uh", days, hours);
    } else {
        snprintf(text, sizeof(text), "~%u.%uh", h10 / 10, h10 % 10);
    }
    lv_label_set_text(eta_label, text);
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_eta, struct eta_state, eta_update_cb, eta_get_state)
ZMK_SUBSCRIPTION(widget_eta, zmk_battery_state_changed);

/* ---- screen --------------------------------------------------------------- */

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
    zmk_widget_battery_status_init(&battery_status_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_status_widget), LV_ALIGN_TOP_RIGHT, 0, 0);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
    zmk_widget_peripheral_status_init(&peripheral_status_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_status_widget), LV_ALIGN_TOP_LEFT, 0,
                 0);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_set_style_text_font(zmk_widget_layer_status_obj(&layer_status_widget),
                               lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_LEFT, 0, 0);
#endif

    cu_label_ref = zmk_claude_usage_create(screen);
    if (cu_label_ref != NULL) {
        lv_obj_set_style_text_font(cu_label_ref, &lv_font_unscii_8, LV_PART_MAIN);
        lv_obj_align(cu_label_ref, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_add_flag(cu_label_ref, LV_OBJ_FLAG_HIDDEN); /* battery boot default */
    }

    eta_label = lv_label_create(screen);
    lv_obj_set_style_text_font(eta_label, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_align(eta_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    widget_eta_init();

    return screen;
}
