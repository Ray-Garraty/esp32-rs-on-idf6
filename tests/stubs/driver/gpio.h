#pragma once

#include <cstdint>

typedef enum {
    GPIO_NUM_NC = -1,
    GPIO_NUM_0 = 0,
    GPIO_NUM_1 = 1,
    GPIO_NUM_2 = 2,
    GPIO_NUM_3 = 3,
    GPIO_NUM_4 = 4,
    GPIO_NUM_13 = 13,
    GPIO_NUM_5 = 5,
    GPIO_NUM_6 = 6,
    GPIO_NUM_7 = 7,
    GPIO_NUM_14 = 14,
    GPIO_NUM_15 = 15,
    GPIO_NUM_16 = 16,
    GPIO_NUM_17 = 17,
    GPIO_NUM_21 = 21,
    GPIO_NUM_26 = 26,
    GPIO_NUM_27 = 27,
    GPIO_NUM_33 = 33,
    GPIO_NUM_34 = 34,
    GPIO_NUM_35 = 35,
    GPIO_NUM_48 = 48,
} gpio_num_t;

typedef enum {
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_INPUT_OUTPUT
} gpio_mode_t;

inline void gpio_set_level(gpio_num_t, uint32_t) {}
inline void gpio_set_direction(gpio_num_t, gpio_mode_t) {}
