#ifndef     __BOARD_LED_H
#define     __BOARD_LED_H

typedef enum
{
    LED_ON = 0,
    LED_OFF = 1
}led_state_t;

void led_on(void);
void led_off(void);
void led_toggle(void);
#endif