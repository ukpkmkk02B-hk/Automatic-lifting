#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f10x.h"

// 模    块：有源蜂鸣器驱动
// 硬件假设：PA0 接低电平触发蜂鸣器模块，GPIO 高电平为静音安全态。
// 安全约束：蜂鸣器静音只影响声音提示，故障锁存和清除必须由 error_manager 处理。

// 函    数：Buzzer_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 PA0 后立即输出静音电平，避免上电或复位过程中误报警。
void Buzzer_Init(void);

// 函    数：Buzzer_On
// 参    数：无
// 返 回 值：无
// 注意事项：只打开声音提示，不设置任何错误码。
void Buzzer_On(void);

// 函    数：Buzzer_Off
// 参    数：无
// 返 回 值：无
// 注意事项：只关闭声音提示，不清除已经锁存的故障。
void Buzzer_Off(void);

// 函    数：Buzzer_Set
// 参    数：on 非 0 表示鸣叫，0 表示关闭。
// 返 回 值：无
// 注意事项：这是输出便捷接口，不改变 error_manager 的静音标志或故障状态。
void Buzzer_Set(uint8_t on);

// 函    数：Buzzer_IsOn
// 参    数：无
// 返 回 值：1 表示当前软件命令为鸣叫，0 表示关闭。
// 注意事项：返回的是软件输出记录，不检测蜂鸣器模块实际硬件状态。
uint8_t Buzzer_IsOn(void);

#endif
