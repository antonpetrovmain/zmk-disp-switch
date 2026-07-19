# zmk-disp-switch

ZMK behavior module: turn the OLED(s) of a (split) keyboard off/on from the
keymap, via display blanking — not ext-power. The display controller stays
powered and initialized, so:

- OFF: panel dark, ~10 uA, typing completely unaffected (no I2C timeout storm)
- ON: instant relight, no reboot needed
- `BEHAVIOR_LOCALITY_GLOBAL`: runs on both split halves
- Re-asserts the user's OFF state after ZMK's idle/active display handling
  unblanks the screen on wake (30 ms delayed re-blank)

## Usage

west.yml: add this repo as a project. Keymap:

```dts
/ {
    behaviors {
        // Node name MUST be <= 8 chars: ZMK's split relay sends behaviors
        // by device name in a 9-byte field; longer names truncate and the
        // peripheral silently drops the press.
        disp_sw: disp_sw {
            compatible = "zmk,behavior-disp-switch";
            #binding-cells = <1>;
        };
    };
};
```

Bind `&disp_sw 0` = screens off, `&disp_sw 1` = screens on, `&disp_sw 2` = toggle.
Params are idempotent — pressing OFF again on a half that missed the relay heals it.
