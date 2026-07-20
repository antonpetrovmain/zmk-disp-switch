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

#include <lvgl.h>
#include <zmk/behavior.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif
#if defined(CONFIG_SOC_SERIES_NRF52X)
#include <hal/nrf_power.h>
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
#if defined(CONFIG_SOC_SERIES_NRF52X)
    /* Raw VBUS from the nRF POWER peripheral. True even on a dumb charger (no
     * data enumeration) and identical on central + peripheral — more reliable
     * than zmk_usb_is_powered(), which stays false when powered by a charger
     * rather than a data host (that's why a cabled half stayed in battery
     * mode). Reading the register needs no USB stack. */
    return nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
#elif IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static void apply_blank_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(apply_blank_work, apply_blank_cb);

/* strong impl in status_screen.c; no-op when the custom screen is unused */
__attribute__((weak)) void disp_sw_layout_refresh(bool usb) { ARG_UNUSED(usb); }

static void apply_blank_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        /* boot race: display comes up async; retry until it is ready */
        k_work_reschedule_for_queue(zmk_display_work_q(), &apply_blank_work, K_MSEC(1000));
        return;
    }
    disp_sw_layout_refresh(usb_powered());
    if (usb_powered() || !user_off || peek_active) {
        display_blanking_off(display);
    } else {
        display_blanking_on(display);
    }
}

static void apply_soon(k_timeout_t delay) {
    k_work_reschedule_for_queue(zmk_display_work_q(), &apply_blank_work, delay);
}

/* ZMK never deep-sleeps on USB power (activity.c), but it still IDLE-blanks
 * at 30 s and STOPS the LVGL tick timer; we re-light the panel, which would
 * freeze the content (stale battery/usage numbers shown as current). Keep the
 * dashboard truthful with a 1 Hz render tick while USB-powered. Duplicate
 * ticks alongside ZMK's own 10 ms timer are serialized on the same work queue
 * and harmless; the chain stops itself when USB power goes away. */
static void slow_tick_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(slow_tick_work, slow_tick_cb);
static void slow_tick_cb(struct k_work *work) {
    if (!usb_powered() || !zmk_display_is_initialized()) {
        return;
    }
    lv_task_handler();
    k_work_reschedule_for_queue(zmk_display_work_q(), &slow_tick_work, K_SECONDS(1));
}

#if defined(CONFIG_SOC_SERIES_NRF52X)
/* Poll raw VBUS and re-apply on change. Needed on the peripheral (no USB stack
 * / events at all) AND on the central (a charger-only connection fires no USB
 * conn event, so events alone miss it). */
static bool vbus_last;
static void vbus_poll_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(vbus_poll_work, vbus_poll_cb);
static void vbus_poll_cb(struct k_work *work) {
    bool now = usb_powered();
    if (now != vbus_last) {
        vbus_last = now;
        apply_soon(K_NO_WAIT);
        if (now) {
            k_work_reschedule_for_queue(zmk_display_work_q(), &slow_tick_work, K_MSEC(100));
        }
    }
    k_work_reschedule(&vbus_poll_work, K_SECONDS(4));
}
#endif

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
        k_work_reschedule_for_queue(zmk_display_work_q(), &slow_tick_work, K_MSEC(100));
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
    k_work_reschedule_for_queue(zmk_display_work_q(), &slow_tick_work, K_SECONDS(4));
#if defined(CONFIG_SOC_SERIES_NRF52X)
    k_work_reschedule(&vbus_poll_work, K_SECONDS(5));
#endif
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
