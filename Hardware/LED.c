#include "stm32f10x.h"
#include "board_config.h"

void LED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(BOARD_LED1_RCC | BOARD_LED2_RCC, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_InitStructure.GPIO_Pin = BOARD_LED1_PIN;
	GPIO_Init(BOARD_LED1_GPIO, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = BOARD_LED2_PIN;
	GPIO_Init(BOARD_LED2_GPIO, &GPIO_InitStructure);

	GPIO_SetBits(BOARD_LED1_GPIO, BOARD_LED1_PIN);
	GPIO_SetBits(BOARD_LED2_GPIO, BOARD_LED2_PIN);
}

void LED1_ON(void)
{
	GPIO_ResetBits(BOARD_LED1_GPIO, BOARD_LED1_PIN);
}

void LED1_OFF(void)
{
	GPIO_SetBits(BOARD_LED1_GPIO, BOARD_LED1_PIN);
}

void LED1_Turn(void)
{
	if (GPIO_ReadOutputDataBit(BOARD_LED1_GPIO, BOARD_LED1_PIN) == 0U)
	{
		LED1_OFF();
	}
	else
	{
		LED1_ON();
	}
}

void LED2_ON(void)
{
	GPIO_ResetBits(BOARD_LED2_GPIO, BOARD_LED2_PIN);
}

void LED2_OFF(void)
{
	GPIO_SetBits(BOARD_LED2_GPIO, BOARD_LED2_PIN);
}

void LED2_Turn(void)
{
	if (GPIO_ReadOutputDataBit(BOARD_LED2_GPIO, BOARD_LED2_PIN) == 0U)
	{
		LED2_OFF();
	}
	else
	{
		LED2_ON();
	}
}
