#ifndef __LED_H
#define __LED_H

// 模    块：板载状态 LED 驱动
// 硬件假设：LED 阳极经限流电阻接 3.3V，PA6/PA7 拉低点亮、拉高熄灭。
// 注意事项：本模块只提供直接输出接口，LED 闪烁节奏由上层状态或主循环调度。

// 函    数：LED_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 LED1/LED2 GPIO，并默认熄灭，避免上电状态误判。
void LED_Init(void);

// 函    数：LED1_ON
// 参    数：无
// 返 回 值：无
// 注意事项：LED1 为低电平点亮。
void LED1_ON(void);

// 函    数：LED1_OFF
// 参    数：无
// 返 回 值：无
// 注意事项：LED1 为高电平熄灭。
void LED1_OFF(void);

// 函    数：LED1_Turn
// 参    数：无
// 返 回 值：无
// 注意事项：只翻转当前输出锁存电平，闪烁周期由上层用时间戳调度。
void LED1_Turn(void);

// 函    数：LED2_ON
// 参    数：无
// 返 回 值：无
// 注意事项：LED2 为低电平点亮。
void LED2_ON(void);

// 函    数：LED2_OFF
// 参    数：无
// 返 回 值：无
// 注意事项：LED2 为高电平熄灭。
void LED2_OFF(void);

// 函    数：LED2_Turn
// 参    数：无
// 返 回 值：无
// 注意事项：只翻转当前输出锁存电平，闪烁周期由上层用时间戳调度。
void LED2_Turn(void);

#endif
