/*
 * behavior_disp_switch — blank/unblank the OLED from the keymap.
 *
 * Unlike &ext_power tricks, this never cuts power: the SSD1306 stays powered
 * and initialized. OFF sends the panel to sleep (~10 uA); ON relights it
 * instantly. Locality is GLOBAL so both split halves act on the same press.
 *
 * ZMK's display module (CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE) unblanks on every
 * idle->active transition, which would undo a manual OFF on the next
 * keypress after idle. We subscribe to the same event and re-assert the
 * user's choice 30 ms later on the display work queue.
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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_CHOSEN(zephyr_display)

#define DS_OFF 0
#define DS_ON 1
#define DS_TOG 2

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static bool user_off;

static void apply_blank_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        return;
    }
    if (user_off) {
        display_blanking_on(display);
    } else {
        display_blanking_off(display);
    }
}
static K_WORK_DELAYABLE_DEFINE(apply_blank_work, apply_blank_cb);

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
    default:
        return -ENOTSUP;
    }
    LOG_DBG("disp_switch: user_off=%d", user_off);
    k_work_schedule_for_queue(zmk_display_work_q(), &apply_blank_work, K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

/* Re-assert OFF after ZMK's idle-wake unblank. */
static int ds_event_cb(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (user_off && ev->state == ZMK_ACTIVITY_ACTIVE) {
        k_work_schedule_for_queue(zmk_display_work_q(), &apply_blank_work, K_MSEC(30));
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(behavior_disp_switch, ds_event_cb);
ZMK_SUBSCRIPTION(behavior_disp_switch, zmk_activity_state_changed);

static const struct behavior_driver_api behavior_disp_switch_driver_api = {
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_disp_switch_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_CHOSEN(zephyr_display) */
