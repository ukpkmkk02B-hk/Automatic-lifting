#ifndef __BOARD_CONFIG_H
#define __BOARD_CONFIG_H

#include "stm32f10x.h"

/* Board clocks */
#define BOARD_RCC_GPIOA                  RCC_APB2Periph_GPIOA
#define BOARD_RCC_GPIOB                  RCC_APB2Periph_GPIOB

/* OLED software I2C, OLED only */
#define BOARD_OLED_SCL_GPIO              GPIOB
#define BOARD_OLED_SCL_PIN               GPIO_Pin_8
#define BOARD_OLED_SDA_GPIO              GPIOB
#define BOARD_OLED_SDA_PIN               GPIO_Pin_9

/* WF5805F software I2C buses */
#define BOARD_I2C_A_SCL_GPIO             GPIOA
#define BOARD_I2C_A_SCL_PIN              GPIO_Pin_1
#define BOARD_I2C_A_SDA_GPIO             GPIOA
#define BOARD_I2C_A_SDA_PIN              GPIO_Pin_2
#define BOARD_I2C_B_SCL_GPIO             GPIOB
#define BOARD_I2C_B_SCL_PIN              GPIO_Pin_6
#define BOARD_I2C_B_SDA_GPIO             GPIOB
#define BOARD_I2C_B_SDA_PIN              GPIO_Pin_7
#define BOARD_I2C_C_SCL_GPIO             GPIOA
#define BOARD_I2C_C_SCL_PIN              GPIO_Pin_8
#define BOARD_I2C_C_SDA_GPIO             GPIOA
#define BOARD_I2C_C_SDA_PIN              GPIO_Pin_9

#define BOARD_OLED_ADDR_7BIT             0x3CU
#define BOARD_WF5805F_ADDR_7BIT          0x6DU

/* UM244 control pins */
#define BOARD_UM244_STEP_GPIO            GPIOA
#define BOARD_UM244_STEP_PIN             GPIO_Pin_3
#define BOARD_UM244_DIR_GPIO             GPIOA
#define BOARD_UM244_DIR_PIN              GPIO_Pin_4
#define BOARD_UM244_MF_GPIO              GPIOA
#define BOARD_UM244_MF_PIN               GPIO_Pin_5

#define BOARD_UM244_STEP_IDLE_LEVEL      Bit_SET
#define BOARD_UM244_STEP_ACTIVE_LEVEL    Bit_RESET
#define BOARD_UM244_MF_HOLD_LEVEL        Bit_SET
#define BOARD_UM244_MF_RELEASE_LEVEL     Bit_RESET
#define BOARD_UM244_DIR_UP_LEVEL         Bit_SET
#define BOARD_UM244_DIR_DOWN_LEVEL       Bit_RESET

/* Limit inputs, optocoupler output low means triggered */
#define BOARD_LIMIT_LEFT_UPPER_GPIO      GPIOB
#define BOARD_LIMIT_LEFT_UPPER_PIN       GPIO_Pin_12
#define BOARD_LIMIT_LEFT_LOWER_GPIO      GPIOB
#define BOARD_LIMIT_LEFT_LOWER_PIN       GPIO_Pin_13
#define BOARD_LIMIT_RIGHT_UPPER_GPIO     GPIOB
#define BOARD_LIMIT_RIGHT_UPPER_PIN      GPIO_Pin_14
#define BOARD_LIMIT_RIGHT_LOWER_GPIO     GPIOB
#define BOARD_LIMIT_RIGHT_LOWER_PIN      GPIO_Pin_15
#define BOARD_LIMIT_ACTIVE_LEVEL         Bit_RESET

#define BOARD_LIMIT_SAMPLE_MS            5U
#define BOARD_LIMIT_TRIGGER_CONFIRM_MS   20U
#define BOARD_LIMIT_RELEASE_CONFIRM_MS   50U

/* Key inputs, active low */
#define BOARD_KEY1_GPIO                  GPIOB
#define BOARD_KEY1_PIN                   GPIO_Pin_1
#define BOARD_KEY2_GPIO                  GPIOB
#define BOARD_KEY2_PIN                   GPIO_Pin_11
#define BOARD_KEY_PAUSE_GPIO             GPIOB
#define BOARD_KEY_PAUSE_PIN              GPIO_Pin_10
#define BOARD_KEY_PAGE_GPIO              GPIOB
#define BOARD_KEY_PAGE_PIN               GPIO_Pin_0
#define BOARD_KEY_ACTIVE_LEVEL           Bit_RESET

#define BOARD_KEY_SCAN_PERIOD_MS         10U
#define BOARD_KEY_DEBOUNCE_MS            25U
#define BOARD_KEY_SHORT_MIN_MS           25U
#define BOARD_KEY_SHORT_MAX_MS           1000U
#define BOARD_KEY_LONG_MS                1000U
#define BOARD_KEY_MAINTENANCE_MS         3000U

/* Active buzzer, low level sounds */
#define BOARD_BUZZER_GPIO                GPIOA
#define BOARD_BUZZER_PIN                 GPIO_Pin_0
#define BOARD_BUZZER_ON_LEVEL            Bit_RESET
#define BOARD_BUZZER_OFF_LEVEL           Bit_SET

/* Existing LEDs */
#define BOARD_LED1_RCC                   BOARD_RCC_GPIOA
#define BOARD_LED1_GPIO                  GPIOA
#define BOARD_LED1_PIN                   GPIO_Pin_6
#define BOARD_LED2_RCC                   BOARD_RCC_GPIOA
#define BOARD_LED2_GPIO                  GPIOA
#define BOARD_LED2_PIN                   GPIO_Pin_7

/* Motion constants for later stages */
#define BOARD_STEPPER_PULSE_PER_REV      1600U
#define BOARD_LEADSCREW_MM_PER_REV_X10   20U
#define BOARD_STEPPER_PULSE_PER_MM       800U
#define BOARD_NAP_DEFAULT_PULSES         8U
#define BOARD_NAP_MAX_PULSES             16U
#define BOARD_DIR_SETUP_HOLD_MS          5U
#define BOARD_STEPPER_AUTO_FREQ_HZ       800U
#define BOARD_STEPPER_FALLBACK_FREQ_HZ   400U
#define BOARD_STEPPER_MANUAL_FREQ_HZ     800U
#define BOARD_STEPPER_HOMING_FREQ_HZ     400U
#define BOARD_STEPPER_MAX_FREQ_HZ        5000U

/* Water depth and sensor health constants */
#define BOARD_WATER_FILTER_SAMPLES       5U
#define BOARD_TANK_MIN_DEPTH_MM          250
#define BOARD_TANK_MAX_DEPTH_MM          450
#define BOARD_BASKET_MIN_SAFE_DEPTH_MM   5
#define BOARD_BASKET_MAX_SAFE_DEPTH_MM   120
#define BOARD_WATER_JUMP_MM_PER_MIN      10
#define BOARD_SENSOR_FAILURE_LIMIT       5U
#define BOARD_I2C_RECOVERY_FAILURE_LIMIT 5U
#define BOARD_PRESSURE_PHYSICAL_MIN_MM_X10 (-20)

#endif
