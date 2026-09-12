#include    "Board_LED.h"
#include   "gpio.h"
#include   "main.h"

static led_state_t led_state;

void led_on(void)
{
    led_state = LED_ON;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, led_state);
}

void led_off(void)
{
    led_state = LED_OFF;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, led_state);
}

void led_toggle(void)
{
    led_state = (led_state == LED_ON) ? LED_OFF : LED_ON;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, led_state);
}