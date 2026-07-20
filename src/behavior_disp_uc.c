/*
 * behavior_disp_uc — data-carrier behavior for daily model-cost sync.
 * param1 = opus<<20 | sonnet<<10 | fable (10 bits each, integer dollars,
 * 1023 = unknown/overflow). param2 = haiku (10 bits, same encoding) — carried
 * for the central's left-side "O S H" line; the peripheral ignores it and
 * renders only Fable. GLOBAL locality relays both params to the peripheral.
 * Node name <= 8 chars.
 *
 * SPDX-License-Identifier: MIT
 */
#define DT_DRV_COMPAT zmk_behavior_disp_ucost

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

__attribute__((weak)) void zmk_costs_set(int o, int s, int f, int h) {}

static int dec(uint32_t v) { return v >= 1023 ? -1 : (int)v; }

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    uint32_t p = binding->param1;
    zmk_costs_set(dec((p >> 20) & 0x3FF), dec((p >> 10) & 0x3FF), dec(p & 0x3FF),
                  dec(binding->param2 & 0x3FF));
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
