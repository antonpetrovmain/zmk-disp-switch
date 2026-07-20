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

/* Own layer indicator: just the active layer NUMBER (the stock widget prefixes
 * LV_SYMBOL_KEYBOARD, which crowded the left OLED). Central only. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/keymap.h>
#include <zmk/events/layer_state_changed.h>

static lv_obj_t *layer_label;

struct layer_state {
    uint8_t index;
};

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    return (struct layer_state){.index = zmk_keymap_highest_layer_active()};
}

static void layer_update_cb(struct layer_state st) {
    if (layer_label != NULL) {
        lv_label_set_text_fmt(layer_label, "%d", st.index);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_num, struct layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(widget_layer_num, zmk_layer_state_changed);
#endif

/* ---- compact output indicator (central only) ------------------------------
 * USB: the USB glyph. BLE: BT glyph + profile number (1-based, pixel font):
 * underlined = connected, bare = bonded but away, '*' = pairing open.
 * Replaces the stock widget whose [wifi][n][check] trio ate the top row. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#endif

static lv_obj_t *out_icon_label;
static lv_obj_t *out_digit_label;
static lv_obj_t *out_underline;

struct out_state {
    struct zmk_endpoint_instance ep;
    bool connected;
    bool bonded;
};

static struct out_state out_get_state(const zmk_event_t *eh) {
    return (struct out_state){.ep = zmk_endpoints_selected(),
                              .connected = zmk_ble_active_profile_is_connected(),
                              .bonded = !zmk_ble_active_profile_is_open()};
}

static void out_update_cb(struct out_state st) {
    if (out_icon_label == NULL) {
        return;
    }
    if (st.ep.transport == ZMK_TRANSPORT_USB) {
        lv_label_set_text(out_icon_label, LV_SYMBOL_USB);
        lv_obj_add_flag(out_digit_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(out_underline, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(out_icon_label, LV_SYMBOL_BLUETOOTH);
        if (st.bonded) {
            lv_label_set_text_fmt(out_digit_label, "%d", st.ep.ble.profile_index + 1);
        } else {
            lv_label_set_text(out_digit_label, "*");
        }
        lv_obj_clear_flag(out_digit_label, LV_OBJ_FLAG_HIDDEN);
        if (st.bonded && st.connected) {
            lv_obj_clear_flag(out_underline, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(out_underline, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_out_compact, struct out_state, out_update_cb, out_get_state)
ZMK_SUBSCRIPTION(widget_out_compact, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_out_compact, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_out_compact, zmk_usb_conn_state_changed);
#endif
#else /* peripheral: compact split-link indicator mirroring the central style —
       * BT glyph + 2 px underline when connected (replaces stock [wifi][✓]) */
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>

static lv_obj_t *p_icon_label;
static lv_obj_t *p_underline;

struct perif_state {
    bool connected;
};

static struct perif_state perif_get_state(const zmk_event_t *eh) {
    return (struct perif_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void perif_update_cb(struct perif_state st) {
    if (p_underline == NULL) {
        return;
    }
    if (st.connected) {
        lv_obj_clear_flag(p_underline, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(p_underline, LV_OBJ_FLAG_HIDDEN);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_perif_compact, struct perif_state, perif_update_cb,
                            perif_get_state)
ZMK_SUBSCRIPTION(widget_perif_compact, zmk_split_peripheral_status_changed);
#endif /* central/peripheral */

/* ---- Claude usage label (usage_display.c) --------------------------------- */

lv_obj_t *zmk_usage_display_create(lv_obj_t *parent);
lv_obj_t *zmk_costs_display_create(lv_obj_t *parent);

/* ---- battery ETA label ---------------------------------------------------- */

static char eta_text[12]; /* rendered inline before the battery %% */
static lv_obj_t *cu_label_ref;
static lv_obj_t *batt_pct_label;
static lv_obj_t *batt_icon_label;
static uint8_t batt_soc;
static bool batt_usb;

/* Own battery cluster: ONE glyph that swaps to the charge bolt while
 * powered (the stock widget APPENDS the bolt as a second glyph, which
 * overlapped the percentage), plus a fixed-offset pixel-font percentage. */
static void render_batt(void) {
    if (batt_icon_label == NULL) {
        return;
    }
    if (batt_pct_label != NULL) {
        /* one line, right-anchored, grows leftward: "2d15h 86". ETA is shown
         * whenever we have one — NOT gated on charge state. (It used to hide
         * while batt_usb was true, but USB/VBUS detection proved unreliable on
         * this hardware and kept hiding the ETA on battery; the charge BOLT
         * icon already signals charging, so the ETA can safely stay put.) */
        if (eta_text[0] != '\0') {
            lv_label_set_text_fmt(batt_pct_label, "%s %02u", eta_text, batt_soc);
        } else {
            lv_label_set_text_fmt(batt_pct_label, "%02u", batt_soc);
        }
        /* low-battery alert: invert the reading when critically low. Based on
         * SOC alone (not batt_usb) so an unreliable USB signal can't suppress
         * the warning. */
        bool low = batt_soc <= CONFIG_ZMK_DISP_SW_BATT_LOW_PCT;
        lv_obj_set_style_bg_color(batt_pct_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(batt_pct_label, low ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(batt_pct_label, low ? lv_color_black() : lv_color_white(),
                                    LV_PART_MAIN);
    }
    const char *sym;
    if (batt_usb) {
        /* charge bolt in a smaller font so it doesn't drop toward the second
         * line (Montserrat 16 bolt hangs low and looked cramped when cabled) */
        lv_obj_set_style_text_font(batt_icon_label, &lv_font_montserrat_12, LV_PART_MAIN);
        sym = LV_SYMBOL_CHARGE;
    } else {
        lv_obj_set_style_text_font(batt_icon_label, &lv_font_montserrat_16, LV_PART_MAIN);
        if (batt_soc > 87) {
            sym = LV_SYMBOL_BATTERY_FULL;
        } else if (batt_soc > 62) {
            sym = LV_SYMBOL_BATTERY_3;
        } else if (batt_soc > 37) {
            sym = LV_SYMBOL_BATTERY_2;
        } else if (batt_soc > 12) {
            sym = LV_SYMBOL_BATTERY_1;
        } else {
            sym = LV_SYMBOL_BATTERY_EMPTY;
        }
    }
    lv_label_set_text(batt_icon_label, sym);
}

/* ---- compact battery % (unscii, next to the stock icon-only widget) ------- */

struct batt_state {
    uint8_t soc;
};

static struct batt_state batt_get_state(const zmk_event_t *eh) {
    return (struct batt_state){.soc = zmk_battery_state_of_charge()};
}

static void batt_update_cb(struct batt_state state) {
    batt_soc = state.soc;
    render_batt();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_batt_pct, struct batt_state, batt_update_cb, batt_get_state)
ZMK_SUBSCRIPTION(widget_batt_pct, zmk_battery_state_changed);

/* Adaptive layout, called by behavior_disp_switch on the display work queue.
 * USB: ETA hidden (meaningless while charging), usage takes bottom-right.
 * Battery: ETA bottom-right, usage moves TOP-CENTER — that space is only
 * crowded on USB (charging bolt); on battery it is free, and BLE now delivers
 * usage data without the cable, so it must stay visible here too. */
void disp_sw_layout_refresh(bool usb) {
    batt_usb = usb;
    render_batt();
}

struct eta_state {
    uint8_t soc;
};

static struct eta_state eta_get_state(const zmk_event_t *eh) {
    return (struct eta_state){.soc = zmk_battery_state_of_charge()};
}

static void eta_update_cb(struct eta_state state) {
    /* hours*10 = soc% * mAh * 100 / uA  (dark-screen drain model) */
    uint32_t h10 = ((uint32_t)state.soc * CONFIG_ZMK_DISP_SW_BATT_MAH * 100) /
                   CONFIG_ZMK_DISP_SW_DRAIN_UA;
    uint32_t days = h10 / 240;
    uint32_t hours = (h10 % 240) / 10;
    if (days > 0) {
        snprintf(eta_text, sizeof(eta_text), "%ud%uh", days, hours);
    } else {
        snprintf(eta_text, sizeof(eta_text), "%u.%uh", h10 / 10, h10 % 10);
    }
    render_batt();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_eta, struct eta_state, eta_update_cb, eta_get_state)
ZMK_SUBSCRIPTION(widget_eta, zmk_battery_state_changed);

/* ---- screen --------------------------------------------------------------- */

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    batt_icon_label = lv_label_create(screen);
    lv_obj_align(batt_icon_label, LV_ALIGN_TOP_RIGHT, 0, 0);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    out_icon_label = lv_label_create(screen);
    lv_obj_set_style_text_font(out_icon_label, &lv_font_montserrat_12, LV_PART_MAIN); /* smaller BT/USB glyph */
    lv_obj_align(out_icon_label, LV_ALIGN_TOP_LEFT, 0, 0);
    out_digit_label = lv_label_create(screen);
    lv_obj_set_style_text_font(out_digit_label, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_align(out_digit_label, LV_ALIGN_TOP_LEFT, 14, 4);
    out_underline = lv_obj_create(screen);
    lv_obj_set_size(out_underline, 8, 2);
    lv_obj_set_style_bg_color(out_underline, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(out_underline, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(out_underline, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(out_underline, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(out_underline, 0, LV_PART_MAIN);
    lv_obj_clear_flag(out_underline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(out_underline, LV_ALIGN_TOP_LEFT, 14, 13);
    widget_out_compact_init();
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    p_icon_label = lv_label_create(screen);
    lv_obj_set_style_text_font(p_icon_label, &lv_font_montserrat_12, LV_PART_MAIN); /* smaller BT glyph */
    lv_label_set_text(p_icon_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(p_icon_label, LV_ALIGN_TOP_LEFT, 0, 0);
    p_underline = lv_obj_create(screen);
    lv_obj_set_size(p_underline, 8, 2);
    lv_obj_set_style_bg_color(p_underline, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_underline, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(p_underline, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(p_underline, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p_underline, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p_underline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(p_underline, LV_ALIGN_TOP_LEFT, 2, 13);
    widget_perif_compact_init();
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    layer_label = lv_label_create(screen);
    lv_obj_set_style_text_font(layer_label, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_align(layer_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(layer_label, "0");
    widget_layer_num_init();
#endif

    lv_obj_t *costs = zmk_costs_display_create(screen);
    if (costs != NULL) {
        lv_obj_set_style_text_font(costs, &lv_font_unscii_8, LV_PART_MAIN);
        lv_obj_set_style_text_align(costs, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        /* "cc" Claude Code badge, centered in the free middle band (limits sit
         * bottom-right, battery top-right, layer bottom-left) */
        lv_obj_align(costs, LV_ALIGN_TOP_MID, 0, 16);
    }

    cu_label_ref = zmk_usage_display_create(screen);
    if (cu_label_ref != NULL) {
        lv_obj_set_style_text_font(cu_label_ref, &lv_font_unscii_8, LV_PART_MAIN);
        lv_obj_set_style_text_align(cu_label_ref, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
        /* the position Tony likes on USB — now permanent in every mode */
        lv_obj_align(cu_label_ref, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
#else
        /* costs pinned to the bottom row (battery/BT own the top) */
        lv_obj_align(cu_label_ref, LV_ALIGN_BOTTOM_MID, 0, 0);
#endif
    }

    batt_pct_label = lv_label_create(screen);
    lv_obj_set_style_text_font(batt_pct_label, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_align(batt_pct_label, LV_ALIGN_TOP_RIGHT, -26, 4);
    widget_batt_pct_init();

    widget_eta_init();

    return screen;
}
