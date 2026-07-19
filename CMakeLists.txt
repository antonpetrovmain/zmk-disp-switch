# Compiled on BOTH split halves — each half drives its own OLED.
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_DISP_SWITCH app PRIVATE src/behavior_disp_switch.c)
target_sources_ifdef(CONFIG_ZMK_DISP_SW_STATUS_SCREEN app PRIVATE src/status_screen.c)
