/*
 * claude_usage — transports for Claude Code usage data (central half only;
 * requires the claude-uart snippet). Two inputs:
 *   USB CDC serial lines: "CU:<0-100>" (5h %), "CW:<0-100>" (7d %),
 *                         "CR:HHMM" (5h reset time, local)
 *   BLE GATT char 4B5C0002-746F-6E79-0001-636C61756465: bytes
 *                         [five, week, hh, mm] (2-4 bytes, 0xFF = unknown)
 *
 * Distribution is ONE path: invoke the disp_us global behavior with all
 * values packed in param1 — ZMK runs it locally (left screen) AND relays it
 * to the peripheral (right screen). Invocation happens from a work item,
 * never from ISR/BT-rx context.
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>
#include <zmk/display.h>

#if DT_HAS_CHOSEN(zmk_claude_uart)

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#endif
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zmk_claude_uart));

/* latest values; 0xFF = unknown */
static atomic_t a_five = ATOMIC_INIT(0xFF);
static atomic_t a_week = ATOMIC_INIT(0xFF);
static atomic_t a_hh = ATOMIC_INIT(0xFF);
static atomic_t a_mm = ATOMIC_INIT(0xFF);
static atomic_t a_o = ATOMIC_INIT(-1); /* $ today per model, in TENTHS */
static atomic_t a_s = ATOMIC_INIT(-1);
static atomic_t a_f = ATOMIC_INIT(-1);

static void distribute_cb(struct k_work *work) {
    uint32_t p = ((uint32_t)(atomic_get(&a_five) & 0xFF) << 24) |
                 ((uint32_t)(atomic_get(&a_week) & 0xFF) << 16) |
                 ((uint32_t)(atomic_get(&a_hh) & 0xFF) << 8) |
                 ((uint32_t)(atomic_get(&a_mm) & 0xFF));
    struct zmk_behavior_binding binding = {.behavior_dev = "disp_us", .param1 = p};
    struct zmk_behavior_binding_event ev = {
        .layer = 0, .position = 0, .timestamp = k_uptime_get()};
    zmk_behavior_invoke_binding(&binding, ev, true);
    zmk_behavior_invoke_binding(&binding, ev, false);

    int o = (int)atomic_get(&a_o), sm = (int)atomic_get(&a_s), f = (int)atomic_get(&a_f);
    if (o >= 0 || sm >= 0 || f >= 0) {
#define UC_ENC(v) ((uint32_t)((v) < 0 ? 0xFFFF : ((v) > 65534 ? 65534 : (v))))
        struct zmk_behavior_binding cost_binding = {
            .behavior_dev = "disp_uc",
            .param1 = (UC_ENC(o) << 16) | UC_ENC(sm),
            .param2 = UC_ENC(f)};
#undef UC_ENC
        zmk_behavior_invoke_binding(&cost_binding, ev, true);
        zmk_behavior_invoke_binding(&cost_binding, ev, false);
    }
}
static K_WORK_DEFINE(distribute_work, distribute_cb);

/* ---- USB CDC serial ------------------------------------------------------ */

static char line[16];
static size_t line_len;

static void handle_line(void) {
    line[line_len] = '\0';
    if (line_len >= 4 && line[0] == 'D' && line[2] == ':') {
        /* DO:/DS:/DF: — dollars spent today per model, in TENTHS ($4.7 => 47) */
        int d = atoi(&line[3]);
        if (d >= 0 && d <= 65535) {
            if (line[1] == 'O') {
                atomic_set(&a_o, d);
            } else if (line[1] == 'S') {
                atomic_set(&a_s, d);
            } else if (line[1] == 'F') {
                atomic_set(&a_f, d);
            } else {
                line_len = 0;
                return;
            }
            k_work_submit(&distribute_work);
        }
        line_len = 0;
        return;
    }
    if (line_len >= 4 && line[0] == 'C' && line[2] == ':') {
        if (line[1] == 'U' || line[1] == 'W') {
            int pct = atoi(&line[3]);
            if (pct >= 0 && pct <= 100) {
                atomic_set(line[1] == 'U' ? &a_five : &a_week, pct);
                k_work_submit(&distribute_work);
            }
        } else if (line[1] == 'R' && line_len == 7) { /* CR:HHMM */
            int hh = (line[3] - '0') * 10 + (line[4] - '0');
            int mm = (line[5] - '0') * 10 + (line[6] - '0');
            if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
                atomic_set(&a_hh, hh);
                atomic_set(&a_mm, mm);
                k_work_submit(&distribute_work);
            }
        }
    }
    line_len = 0;
}

static void uart_cb(const struct device *dev, void *user_data) {
    uint8_t c;
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        while (uart_fifo_read(dev, &c, 1) == 1) {
            if (c == '\n' || c == '\r') {
                if (line_len > 0) {
                    handle_line();
                }
            } else if (line_len < sizeof(line) - 1) {
                line[line_len++] = c;
            } else {
                line_len = 0; /* garbage line, resync */
            }
        }
    }
}

/* (Re)arm RX — SYS_INIT-only arming races the USB stack on some boots. */
static void uart_arm_cb(struct k_work *work) {
    if (!device_is_ready(uart)) {
        return;
    }
    uart_irq_callback_user_data_set(uart, uart_cb, NULL);
    uart_irq_rx_enable(uart);
}
static K_WORK_DELAYABLE_DEFINE(uart_arm_work, uart_arm_cb);

#if IS_ENABLED(CONFIG_ZMK_USB)
static int claude_uart_event_cb(const zmk_event_t *eh) {
    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        k_work_reschedule(&uart_arm_work, K_MSEC(500));
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(claude_uart, claude_uart_event_cb);
ZMK_SUBSCRIPTION(claude_uart, zmk_usb_conn_state_changed);
#endif

/* ---- BLE GATT ------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_BT)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define CLAUDE_SVC_UUID BT_UUID_128_ENCODE(0x4b5c0001, 0x746f, 0x6e79, 0x0001, 0x636c61756465)
#define CLAUDE_CHR_UUID BT_UUID_128_ENCODE(0x4b5c0002, 0x746f, 0x6e79, 0x0001, 0x636c61756465)

static ssize_t claude_usage_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len, uint16_t offset,
                                  uint8_t flags) {
    const uint8_t *b = buf;
    if (offset != 0 || len < 1 || len > 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    if (b[0] <= 100) {
        atomic_set(&a_five, b[0]);
    }
    if (len >= 2 && b[1] <= 100) {
        atomic_set(&a_week, b[1]);
    }
    if (len >= 4 && b[2] <= 23 && b[3] <= 59) {
        atomic_set(&a_hh, b[2]);
        atomic_set(&a_mm, b[3]);
    }
    k_work_submit(&distribute_work);
    return len;
}

#define CLAUDE_COST_UUID BT_UUID_128_ENCODE(0x4b5c0003, 0x746f, 0x6e79, 0x0001, 0x636c61756465)

/* opus, sonnet, fable as uint16 BE dollar TENTHS; 0xFFFF = unknown. 6 bytes
 * normally; an 8-byte write (older haiku host) is accepted and the 4th value
 * ignored. */
static ssize_t claude_cost_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    const uint8_t *b = buf;
    if (offset != 0 || (len != 6 && len != 8)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    atomic_t *slots[3] = {&a_o, &a_s, &a_f};
    for (int i = 0; i < 3; i++) {
        uint16_t v = ((uint16_t)b[i * 2] << 8) | b[i * 2 + 1];
        if (v != 0xFFFF) {
            atomic_set(slots[i], v);
        }
    }
    k_work_submit(&distribute_work);
    return len;
}

BT_GATT_SERVICE_DEFINE(claude_usage_svc,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(CLAUDE_SVC_UUID)),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(CLAUDE_CHR_UUID),
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE_ENCRYPT, NULL,
                                              claude_usage_write, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(CLAUDE_COST_UUID),
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE_ENCRYPT, NULL,
                                              claude_cost_write, NULL), );
#endif /* IS_ENABLED(CONFIG_BT) */

static int claude_uart_init(void) {
    k_work_reschedule(&uart_arm_work, K_SECONDS(1));
    return 0;
}
SYS_INIT(claude_uart_init, APPLICATION, 99);

#endif /* DT_HAS_CHOSEN(zmk_claude_uart) */
