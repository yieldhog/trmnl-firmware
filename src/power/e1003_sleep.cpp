#include <Arduino.h>
#include <config.h>
#include <e1003_sleep.h>

#if defined(BOARD_SEEED_RETERMINAL_E1003)

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <trmnl_log.h>

// Pins per the E1003 v1.0 schematic / Seeed pin map:
//   GPIO11 EPD_Drive_EN  (TPS65185 panel bias rail)     -- FastEPD drives LOW in deInit()
//   GPIO21 ITE_VCC_EN    (IT8951 controller core)        -- FastEPD drives LOW in deInit()
//   GPIO38 PDM_EN        (microphone load switch)        -- unused by this firmware
//   GPIO39 SD_EN         (microSD slot power)            -- unused by this firmware
//   GPIO48 TOUCH_RES     (GT911 reset, ACTIVE-LOW, ext pull-up) -- LOW = controller held in reset
//
// Deliberately NOT in the list:
//   GPIO16 USER_LED (active-low via Q4) -- latching LOW would light the LED all night
//   GPIO40 VBAT_EN  -- battery ADC switch, already managed by the battery code
//   GPIO19/20 I2C   -- disabled as inputs below rather than driven
//   GPIO45 BUZZER_EN -- ESP32-S3 strapping pin (VDD_SPI select, sampled on every
//                       wake reset). The ESPHome reference holds it LOW without
//                       issue, but it is unused here (PIN_BUZZER undefined) and
//                       Seeed's 100k pull-down keeps it off, so leave it alone.
//
// Seeed's schematic has 100k pull-downs on the enables, so once the holds are
// released on boot the rails stay off until the driver raises them.
static const gpio_num_t kHeldLowPins[] = {
    GPIO_NUM_11, GPIO_NUM_21, GPIO_NUM_38, GPIO_NUM_39, GPIO_NUM_48,
};
static const size_t kHeldLowCount = sizeof(kHeldLowPins) / sizeof(kHeldLowPins[0]);

void e1003_release_sleep_holds(void)
{
  // A pad hold outlives deep sleep (and a soft reset). FastEPD's initIT8951()
  // does a LOW->HIGH power cycle on GPIO11/21 with gpio_set_level(), which a
  // held pad silently ignores -- so release everything before the display
  // driver runs. Harmless on a cold boot where nothing is held.
  for (size_t i = 0; i < kHeldLowCount; i++) {
    gpio_hold_dis(kHeldLowPins[i]);
  }
  gpio_deep_sleep_hold_dis();

  // Nothing in TRMNL firmware uses touch or the SD slot, so keep those two
  // enables low while awake as well. This also stops the GT911 scanning
  // during the ~20 s wake window. Remove these four lines if you ever want
  // the stock floating behaviour back while awake.
  pinMode(GPIO_NUM_48, OUTPUT);
  digitalWrite(GPIO_NUM_48, LOW);
  pinMode(GPIO_NUM_39, OUTPUT);
  digitalWrite(GPIO_NUM_39, LOW);
}

static void hold_low(gpio_num_t pin)
{
  gpio_hold_dis(pin);
  pinMode(pin, OUTPUT);
  // Tesserae v1.42.0 finding: a pad hold latches the internal pull-up along
  // with the level, and a pull-up into a pad driven low costs ~70 uA per pin
  // for the whole sleep. Force the pulls off before latching.
  gpio_set_pull_mode(pin, GPIO_FLOATING);
  digitalWrite(pin, LOW);
  gpio_hold_en(pin);
}

void e1003_park_pins_for_sleep(void)
{
  Log_info("E1003: parking peripheral enables for deep sleep");

  // Park the I2C bus (SHT4x / GT911 / PMIC share GPIO19/20). The GT911 is
  // being held in reset below so there is nothing to talk to anyway.
  gpio_set_direction(GPIO_NUM_19, GPIO_MODE_DISABLE);
  gpio_set_direction(GPIO_NUM_20, GPIO_MODE_DISABLE);

  for (size_t i = 0; i < kHeldLowCount; i++) {
    hold_low(kHeldLowPins[i]);
  }

  // GPIO11/21 are RTC-capable and are held by gpio_hold_en() alone. The rest
  // (>= GPIO22) are digital pads and need this call to keep their hold
  // through deep sleep.
  gpio_deep_sleep_hold_en();
}

bool e1003_enable_button_wakeup(int interrupt_pin)
{
#ifdef E1003_SLEEP_USE_EXT0
  // Stock v1.8.17 behaviour, kept as a one-line fallback.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)interrupt_pin, 0);
  return true;
#else
  // Both reference implementations that were verified on this board with the
  // digital hold enabled use ext1 (ANY_LOW) rather than ext0. Keep the RTC
  // peripheral domain on and belt-and-suspenders the pull-up alongside the
  // external one on the button line.
  const gpio_num_t pin = (gpio_num_t)interrupt_pin;
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(pin);
  rtc_gpio_pulldown_dis(pin);
  esp_err_t err = esp_sleep_enable_ext1_wakeup(1ULL << pin, ESP_EXT1_WAKEUP_ANY_LOW);
  if (err != ESP_OK) {
    Log_error("E1003: ext1 wakeup arm failed (%d), falling back to ext0", (int)err);
    esp_sleep_enable_ext0_wakeup(pin, 0);
  }
  return true;
#endif
}

#else // !BOARD_SEEED_RETERMINAL_E1003

void e1003_release_sleep_holds(void) {}
void e1003_park_pins_for_sleep(void) {}
bool e1003_enable_button_wakeup(int) { return false; }

#endif
