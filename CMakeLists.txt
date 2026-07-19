# Compiled on BOTH split halves — each half drives its own OLED.
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_DISP_SWITCH app PRIVATE src/behavior_disp_switch.c)
target_sources_ifdef(CONFIG_ZMK_DISP_SW_STATUS_SCREEN app PRIVATE src/status_screen.c)
target_sources_ifdef(CONFIG_ZMK_DISP_SW_STATUS_SCREEN app PRIVATE src/usage_display.c)
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_DISP_USYNC app PRIVATE src/behavior_disp_usync.c)
# Claude-usage transports: compiles to nothing without the claude-uart snippet.
target_sources_ifdef(CONFIG_ZMK_DISP_SW_STATUS_SCREEN app PRIVATE src/claude_usage.c)
