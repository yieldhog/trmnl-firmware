#ifndef E1003_SLEEP_H
#define E1003_SLEEP_H

// Seeed reTerminal E1003 deep-sleep power fixes (usetrmnl/trmnl-firmware#572).
//
// Problem: the E1003 draws ~5 mA in deep sleep because several peripheral
// enables are never driven, and the ones that are (IT8951 rails) are only
// driven, not held, so the pads float once the chip sleeps. The GT911 touch
// controller in particular has its reset line (GPIO48) pulled HIGH externally,
// so it free-runs in scan mode all night.
//
// Fix (measured on this exact board by ar0v3r/reTerminal-E1003-ESPHome, and
// independently applied by dmellok/tesserae-device-firmware v1.40/v1.42):
//   * drive the enables LOW, with internal pulls OFF, and latch them with
//     gpio_hold_en() + gpio_deep_sleep_hold_en() so they survive deep sleep
//   * release the latches first thing on boot, BEFORE FastEPD power-cycles
//     the IT8951 rails (a held pad ignores gpio_set_level)
//
// Everything here is a no-op unless BOARD_SEEED_RETERMINAL_E1003 is defined.

// Call as early as possible on boot (before display_init / bl_init).
void e1003_release_sleep_holds(void);

// Call immediately before esp_deep_sleep_start(), after display_sleep().
void e1003_park_pins_for_sleep(void);

// Arm the button wake source for the E1003. Replaces the generic ext0 call in
// goToSleep(). Returns true if it armed something.
bool e1003_enable_button_wakeup(int interrupt_pin);

#endif // E1003_SLEEP_H
