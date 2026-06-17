#include "stm32f10x.h"
#include "board_config.h"

// 函    数：LED_Init
// 参    数：无
// 返 回 值：无
// 注意事项：LED 阴极接 GPIO，推挽输出高电平时熄灭、低电平时点亮。
void LED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(BOARD_LED1_RCC | BOARD_LED2_RCC, ENABLE);

	// 两颗 LED 都是普通推挽输出，不在驱动层生成闪烁节奏。
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_InitStructure.GPIO_Pin = BOARD_LED1_PIN;
	GPIO_Init(BOARD_LED1_GPIO, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = BOARD_LED2_PIN;
	GPIO_Init(BOARD_LED2_GPIO, &GPIO_InitStructure);

	// 默认熄灭，避免复位后把 LED 常亮误解为报警状态。
	GPIO_SetBits(BOARD_LED1_GPIO, BOARD_LED1_PIN);
	GPIO_SetBits(BOARD_LED2_GPIO, BOARD_LED2_PIN);
}

// 函    数：LED1_ON
// 参    数：无
// 返 回 值：无
// 注意事项：低电平点亮 LED1。
void LED1_ON(void)
{
	GPIO_ResetBits(BOARD_LED1_GPIO, BOARD_LED1_PIN);
}

// 函    数：LED1_OFF
// 参    数：无
// 返 回 值：无
// 注意事项：高电平熄灭 LED1。
void LED1_OFF(void)
{
	GPIO_SetBits(BOARD_LED1_GPIO, BOARD_LED1_PIN);
}

// 函    数：LED1_Turn
// 参    数：无
// 返 回 值：无
// 注意事项：读取输出锁存位后翻转，供上层非阻塞闪烁调度调用。
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

// 函    数：LED2_ON
// 参    数：无
// 返 回 值：无
// 注意事项：低电平点亮 LED2。
void LED2_ON(void)
{
	GPIO_ResetBits(BOARD_LED2_GPIO, BOARD_LED2_PIN);
}

// 函    数：LED2_OFF
// 参    数：无
// 返 回 值：无
// 注意事项：高电平熄灭 LED2。
void LED2_OFF(void)
{
	GPIO_SetBits(BOARD_LED2_GPIO, BOARD_LED2_PIN);
}

// 函    数：LED2_Turn
// 参    数：无
// 返 回 值：无
// 注意事项：读取输出锁存位后翻转，供上层非阻塞闪烁调度调用。
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
