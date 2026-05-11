#include "buzzer.h"
#include "board_config.h"

static uint8_t s_buzzer_on;

void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOA, ENABLE);

    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_OFF_LEVEL);

    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Pin = BOARD_BUZZER_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_BUZZER_GPIO, &gpio_init);

    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_OFF_LEVEL);
    s_buzzer_on = 0U;
}

void Buzzer_On(void)
{
    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_ON_LEVEL);
    s_buzzer_on = 1U;
}

void Buzzer_Off(void)
{
    GPIO_WriteBit(BOARD_BUZZER_GPIO, BOARD_BUZZER_PIN, BOARD_BUZZER_OFF_LEVEL);
    s_buzzer_on = 0U;
}

void Buzzer_Set(uint8_t on)
{
    if (on != 0U)
    {
        Buzzer_On();
    }
    else
    {
        Buzzer_Off();
    }
}

uint8_t Buzzer_IsOn(void)
{
    return s_buzzer_on;
}
