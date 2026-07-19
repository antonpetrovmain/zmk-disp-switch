/*
 * claude_usage — receive Claude Code 5-hour-limit usage over USB CDC-ACM
 * serial and expose it as an OLED label. Protocol: ASCII lines "CU:<0-100>\n"
 * written by the host-side daemon (claude-usage-to-corne.sh). Compiled to
 * nothing unless the claude-uart snippet provides the zmk,claude-uart chosen.
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <lvgl.h>
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

static lv_obj_t *cu_label;
static bool cu_stacked; /* battery/top-mid mode: render "C56\nW50" (narrow) */
static atomic_t cu_pct = ATOMIC_INIT(-1);
static atomic_t cw_pct = ATOMIC_INIT(-1); /* 7-day window */

static void cu_update_cb(struct k_work *work) {
    if (cu_label == NULL) {
        return;
    }
    int pct = (int)atomic_get(&cu_pct);
    int wk = (int)atomic_get(&cw_pct);
    char text[16];
    if (pct >= 0 && pct <= 100 && wk >= 0 && wk <= 100) {
        /* C = current 5h window, W = 7-day week */
        snprintf(text, sizeof(text), cu_stacked ? "C%d\nW%d" : "C%d W%d", pct, wk);
    } else if (pct >= 0 && pct <= 100) {
        snprintf(text, sizeof(text), "C%d", pct);
    } else {
        snprintf(text, sizeof(text), "C--");
    }
    lv_label_set_text(cu_label, text);
}
static K_WORK_DEFINE(cu_update_work, cu_update_cb);

/* line accumulator, filled from UART ISR */
static char line[16];
static size_t line_len;

static void handle_line(void) {
    line[line_len] = '\0';
    if (line_len >= 4 && line[0] == 'C' && line[2] == ':' &&
        (line[1] == 'U' || line[1] == 'W')) {
        int pct = atoi(&line[3]);
        if (pct >= 0 && pct <= 100) {
            atomic_set(line[1] == 'U' ? &cu_pct : &cw_pct, pct);
            k_work_submit_to_queue(zmk_display_work_q(), &cu_update_work);
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

#if IS_ENABLED(CONFIG_BT)
/* Custom GATT service: the bonded host writes 2 bytes [five_hour, seven_day]
 * (0-100; 0xFF = unknown) over the existing BLE bond — usage updates without
 * the USB cable. UUID x-...-636c61756465 spells "claude". Encrypted writes
 * only, so only bonded hosts can touch it. */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define CLAUDE_SVC_UUID BT_UUID_128_ENCODE(0x4b5c0001, 0x746f, 0x6e79, 0x0001, 0x636c61756465)
#define CLAUDE_CHR_UUID BT_UUID_128_ENCODE(0x4b5c0002, 0x746f, 0x6e79, 0x0001, 0x636c61756465)

static ssize_t claude_usage_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len, uint16_t offset,
                                  uint8_t flags) {
    const uint8_t *b = buf;
    if (offset != 0 || len < 1 || len > 2) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    if (b[0] <= 100) {
        atomic_set(&cu_pct, b[0]);
    }
    if (len == 2 && b[1] <= 100) {
        atomic_set(&cw_pct, b[1]);
    }
    k_work_submit_to_queue(zmk_display_work_q(), &cu_update_work);
    return len;
}

BT_GATT_SERVICE_DEFINE(claude_usage_svc,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(CLAUDE_SVC_UUID)),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(CLAUDE_CHR_UUID),
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE_ENCRYPT, NULL,
                                              claude_usage_write, NULL), );
#endif /* IS_ENABLED(CONFIG_BT) */

/* Called by the custom status screen on layout changes: stacked (two-line,
 * narrow, for the battery-mode top-center slot squeezed between the BLE
 * output widget and the battery cluster) vs single-line (USB bottom-right). */
void zmk_claude_usage_set_stacked(bool stacked) {
    if (cu_stacked != stacked) {
        cu_stacked = stacked;
        k_work_submit_to_queue(zmk_display_work_q(), &cu_update_work);
    }
}

/* Called by the custom status screen; strong override of the weak stub. */
lv_obj_t *zmk_claude_usage_create(lv_obj_t *parent) {
    cu_label = lv_label_create(parent);
    lv_label_set_text(cu_label, "C--");
    return cu_label;
}

/* (Re)arm RX. Arming once at SYS_INIT races the USB stack on some boots —
 * RX stays silently dead until the next lucky boot (field-observed: same
 * firmware worked after one flash, not after another). Idempotent, so we
 * re-arm on every USB connection event plus staggered boot retries. */
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

static int claude_uart_init(void) {
    if (!device_is_ready(uart)) {
        LOG_WRN("claude-uart device not ready");
    }
    k_work_reschedule(&uart_arm_work, K_SECONDS(1));
    /* second staggered retry in case second 1 also raced */
    k_work_reschedule(&uart_arm_work, K_SECONDS(1));
    return 0;
}
SYS_INIT(claude_uart_init, APPLICATION, 99);

#endif /* DT_HAS_CHOSEN(zmk_claude_uart) */
