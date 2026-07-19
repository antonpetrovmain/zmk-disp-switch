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
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zmk_claude_uart));

static lv_obj_t *cu_label;
static atomic_t cu_pct = ATOMIC_INIT(-1);

static void cu_update_cb(struct k_work *work) {
    if (cu_label == NULL) {
        return;
    }
    int pct = (int)atomic_get(&cu_pct);
    char text[10];
    if (pct >= 0 && pct <= 100) {
        snprintf(text, sizeof(text), "C%d%%", pct);
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
    if (line_len >= 4 && line[0] == 'C' && line[1] == 'U' && line[2] == ':') {
        int pct = atoi(&line[3]);
        if (pct >= 0 && pct <= 100) {
            atomic_set(&cu_pct, pct);
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

/* Called by the custom status screen; strong override of the weak stub. */
lv_obj_t *zmk_claude_usage_create(lv_obj_t *parent) {
    cu_label = lv_label_create(parent);
    lv_label_set_text(cu_label, "C--");
    return cu_label;
}

static int claude_uart_init(void) {
    if (!device_is_ready(uart)) {
        LOG_WRN("claude-uart device not ready");
        return 0;
    }
    uart_irq_callback_user_data_set(uart, uart_cb, NULL);
    uart_irq_rx_enable(uart);
    return 0;
}
SYS_INIT(claude_uart_init, APPLICATION, 99);

#endif /* DT_HAS_CHOSEN(zmk_claude_uart) */
