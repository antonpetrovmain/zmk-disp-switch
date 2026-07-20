/*
 * behavior_disp_uc — data-carrier behavior for daily model-cost sync.
 * param1 = opus<<16 | sonnet ; param2 = fable. Each is a 16-bit dollar amount
 * in TENTHS (0..65534; 0xFFFF = unknown/overflow) — wide enough now that Haiku
 * was dropped and freed the bits. GLOBAL locality relays both params to the
 * peripheral, which renders "O$x.x S$x.x F$x.x". Node name <= 8 chars.
 *
 * SPDX-License-Identifier: MIT
 */
#define DT_DRV_COMPAT zmk_behavior_disp_ucost

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

__attribute__((weak)) void zmk_costs_set(int o, int s, int f) {}

static int dec(uint32_t v) { return v >= 0xFFFF ? -1 : (int)v; }

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    uint32_t p1 = binding->param1;
    zmk_costs_set(dec((p1 >> 16) & 0xFFFF), dec(p1 & 0xFFFF), dec(binding->param2 & 0xFFFF));
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
