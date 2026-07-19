/*
 * behavior_disp_switch — OLED power UX for split keyboards, via display
 * blanking (never ext-power):
 *
 *   &disp_sw 0  screens off (sets user-dark)
 *   &disp_sw 1  screens on  (clears user-dark)
 *   &disp_sw 2  toggle
 *   &disp_sw 3  smart peek: if dark -> show for ZMK_DISP_SW_PEEK_MS then
 *               re-dark; if manually lit on battery -> go dark now
 *
 * USB-AWARE AUTO-DARK: each half watches ITS OWN USB state — plugged in =>
 * screen on (power is free), on battery => user-dark applies (default dark
 * from boot). Deliberately per-half: when only the central is cabled, the
 * peripheral stays dark because lighting it would burn the peripheral's
 * battery — use peek to glance at it.
 *
 * Locality GLOBAL => one press acts on both halves (node name MUST stay
 * <= 8 chars, see README). Params idempotent; re-press heals a missed relay.
 *
 * SPDX-License-Identifier: MIT
 */
#define DT_DRV_COMPAT zmk_behavior_disp_switch

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_CHOSEN(zephyr_display)

#define DS_OFF 0
#define DS_ON 1
#define DS_TOG 2
#define DS_PEEK 3

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static bool user_off = true; /* battery default: dark from boot */
static bool peek_active;

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static void apply_blank_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(apply_blank_work, apply_blank_cb);

static void apply_blank_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        /* boot race: display comes up async; retry until it is ready */
        k_work_reschedule_for_queue(zmk_display_work_q(), &apply_blank_work, K_MSEC(1000));
        return;
    }
    if (usb_powered() || !user_off || peek_active) {
        display_blanking_off(display);
    } else {
        display_blanking_on(display);
    }
}

static void apply_soon(k_timeout_t delay) {
    k_work_reschedule_for_queue(zmk_display_work_q(), &apply_blank_work, delay);
}

static void peek_end_cb(struct k_work *work) {
    peek_active = false;
    apply_soon(K_NO_WAIT);
}
static K_WORK_DELAYABLE_DEFINE(peek_end_work, peek_end_cb);

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case DS_OFF:
        user_off = true;
        break;
    case DS_ON:
        user_off = false;
        break;
    case DS_TOG:
        user_off = !user_off;
        break;
    case DS_PEEK:
        if (!user_off && !usb_powered()) {
            /* manually lit on battery: peek key doubles as "go dark now" */
            user_off = true;
        } else if (user_off) {
            peek_active = true;
            k_work_reschedule_for_queue(zmk_display_work_q(), &peek_end_work,
                                        K_MSEC(CONFIG_ZMK_DISP_SW_PEEK_MS));
        }
        break;
    default:
        return -ENOTSUP;
    }
    LOG_DBG("disp_sw: user_off=%d peek=%d usb=%d", user_off, peek_active, usb_powered());
    apply_soon(K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

/* Re-assert our state after ZMK's idle-wake unblank, and react to USB
 * plug/unplug. 30 ms delay lets ZMK's own display listener run first. */
static int ds_event_cb(const zmk_event_t *eh) {
    if (as_zmk_activity_state_changed(eh) != NULL) {
        apply_soon(K_MSEC(30));
        return ZMK_EV_EVENT_BUBBLE;
    }
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        apply_soon(K_MSEC(30));
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(behavior_disp_switch, ds_event_cb);
ZMK_SUBSCRIPTION(behavior_disp_switch, zmk_activity_state_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(behavior_disp_switch, zmk_usb_conn_state_changed);
#endif

/* First apply shortly after boot so a battery-powered boot goes dark. */
static int disp_sw_sysinit(void) {
    apply_soon(K_SECONDS(3));
    return 0;
}
SYS_INIT(disp_sw_sysinit, APPLICATION, 99);

static const struct behavior_driver_api behavior_disp_switch_driver_api = {
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_disp_switch_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_CHOSEN(zephyr_display) */
