#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

#include "card_reader_gpio.h"

#define GPIO_DEBUG_EN

#ifdef GPIO_DEBUG_EN
#define GPIO_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define GPIO_DEBUG(fmt, ...)
#endif

static const char* TAG = "card_reader_gpio";

/**
* @brief Confugure GPIO pins for use
*/
void gpio_setup() {
  // Onboard LED pin
  gpio_pad_select_gpio(PIN_ONBOARD_LED);
  gpio_set_direction(PIN_ONBOARD_LED, GPIO_MODE_OUTPUT);
  gpio_setOnboardLed(LED_OFF);

  // Indicator RG LED pins
  gpio_pad_select_gpio(PIN_INDICATOR_LED_G);
  gpio_pad_select_gpio(PIN_INDICATOR_LED_R);
  gpio_set_direction(PIN_INDICATOR_LED_G, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_INDICATOR_LED_R, GPIO_MODE_OUTPUT);
  gpio_setIndicatorLed(LED_OFF);

  // Battery status ADC pin
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(PIN_POW_BATT, ADC_ATTEN_DB_0);

  // Power status pin
  gpio_pad_select_gpio(PIN_POW_SOURCE);
  gpio_set_direction(PIN_POW_SOURCE, GPIO_MODE_INPUT);

  gpio_setIndicatorLed(LED_GREEN);
  vTaskDelay(200 / portTICK_PERIOD_MS); // Wait 200 ms
  gpio_setIndicatorLed(LED_OFF);

  ESP_LOGI(TAG, "GPIO module set up!");
}

/**
* @brief  Turn onboard LED on or off
*
* @param  state   Code of the state (0 = LED_OFF, 1 = LED_ON)
*/
void gpio_setOnboardLed(uint8_t state) {
  if(state) {
    gpio_set_level(PIN_ONBOARD_LED, 1);
  }
  else {
    gpio_set_level(PIN_ONBOARD_LED, 0);
  }
}

/**
* @brief  Set color of indicator RG LED or turn it off
*
* @param  state   Code of the state (0 = LED_OFF,
*                                    1 = LED_RED,
*                                    2 = LED_GREEN,
*                                    3 = LED_ORANGE)
*/
void gpio_setIndicatorLed(uint8_t state) {
  switch (state) {
    case LED_OFF:
      gpio_set_level(PIN_INDICATOR_LED_G, 1);
      gpio_set_level(PIN_INDICATOR_LED_R, 1);
      GPIO_DEBUG("Indicator LED: off\n");
      break;
    case LED_RED:
      gpio_set_level(PIN_INDICATOR_LED_G, 1);
      gpio_set_level(PIN_INDICATOR_LED_R, 0);
      GPIO_DEBUG("Indicator LED: red\n");
      break;
    case LED_GREEN:
      gpio_set_level(PIN_INDICATOR_LED_G, 0);
      gpio_set_level(PIN_INDICATOR_LED_R, 1);
      GPIO_DEBUG("Indicator LED: green\n");
      break;
    case LED_ORANGE:
      gpio_set_level(PIN_INDICATOR_LED_G, 0);
      gpio_set_level(PIN_INDICATOR_LED_R, 0);
      GPIO_DEBUG("Indicator LED: orange\n");
      break;
    default:
      ESP_LOGW(TAG, "Attempt to set Indicator LED to undefined state: %d\n", state);
      break;
  }
}

/**
* @brief    Read voltage on the battery ADC pin and calculate battery volatge
*
* @return   Battery voltage in mV
*/
uint32_t gpio_getBatteryVoltage() {
  uint32_t raw = 0;
  for(int i = 0; i < ADC_SAMPLES; ++i) {
    raw += adc1_get_raw(PIN_POW_BATT);
  }
  raw /= ADC_SAMPLES;
  uint32_t voltage = ((raw * ADC_REF_VOLTAGE) / ADC_LEVELS_BIT_12) * VOLT_DIV_CONST;
  GPIO_DEBUG("Battery voltage raw on ADC: %d, mV: %d\n", raw, voltage);
  return voltage;
}

/**
* @brief    Check if battery volatge is bellow critical treshold
*
* @return   1 = battery critical, 0 = battery not critical
*/
uint8_t gpio_isBatteryCritical() {
  uint8_t ret = gpio_getBatteryVoltage() < BATT_CRITICAL_VOLTAGE;
  GPIO_DEBUG("Battery critical: %s\n", ret ? "yes" : "no");
  return ret;
}

/**
* @brief    Read power status of the device
*
* @return   1 = device is source powered, 0 = device is battery powered
*/
uint8_t gpio_isSourcePowered() {
  uint8_t ret = gpio_get_level(PIN_POW_SOURCE);
  GPIO_DEBUG("Source powered: %s\n", ret ? "Yes" : "No");
  return ret;
}
