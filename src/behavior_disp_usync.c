/*
 * behavior_disp_usync — data-carrier behavior for Claude usage sync.
 * Never bound to a key: the central invokes it programmatically with
 * param1 = five<<24 | week<<16 | hh<<8 | mm (0xFF = unknown byte), and the
 * GLOBAL locality makes ZMK relay it to the peripheral, whose display then
 * updates. Node name MUST stay <= 8 chars (split relay DEV_LEN).
 *
 * SPDX-License-Identifier: MIT
 */
#define DT_DRV_COMPAT zmk_behavior_disp_usync

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

__attribute__((weak)) void zmk_usage_set(int five, int week, int hh, int mm) {}

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    uint32_t p = binding->param1;
    uint8_t five = (p >> 24) & 0xFF, week = (p >> 16) & 0xFF;
    uint8_t hh = (p >> 8) & 0xFF, mm = p & 0xFF;
    zmk_usage_set(five == 0xFF ? -1 : five, week == 0xFF ? -1 : week,
                  hh == 0xFF ? -1 : hh, mm == 0xFF ? -1 : mm);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api = {
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
    .binding_pressed = on_pressed,
    .binding_released = on_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

#endif
