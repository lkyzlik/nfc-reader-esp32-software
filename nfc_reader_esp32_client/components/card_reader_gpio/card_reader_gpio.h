#ifndef __GPIO_H__
#define __GPIO_H__

#define PIN_ONBOARD_LED 2
#define PIN_INDICATOR_LED_R 17
#define PIN_INDICATOR_LED_G 16
#define PIN_POW_BATT ADC1_CHANNEL_0 // GPIO36
#define PIN_POW_SOURCE 4

#define ADC_SAMPLES 64 // Number of samples for one ADC measuremet
#define ADC_REF_VOLTAGE 1100 // 1.1V
#define ADC_LEVELS_BIT_12 4096 // ADC voltage levels for 12 bits

#define VOLT_DIV_CONST 6 // Multiplyer constat to compensate for volatge divider
#define BATT_CRITICAL_VOLTAGE 3600

#define LED_OFF 0
#define LED_ON 1
#define LED_RED 1
#define LED_GREEN 2
#define LED_ORANGE 3

void gpio_setup();
void gpio_setOnboardLed(uint8_t state);
void gpio_setIndicatorLed(uint8_t state);
uint32_t gpio_getBatteryVoltage();
uint8_t gpio_isBatteryCritical();
uint8_t gpio_isSourcePowered();

#endif
