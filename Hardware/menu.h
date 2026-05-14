#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"

// 模    块：OLED 菜单与按键交互
// 职    责：消费 key_scan 事件，驱动 ui_pages 显示，处理参数编辑、报警静音、维护确认和手动点动。
// 安全假设：阶段 7 不实现完整 app_state；所有运动请求仍通过 stepper_um244 的有限脉冲和限位保护接口执行。

// 函    数：Menu_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：初始化菜单状态并读取 Flash 参数缓存；不主动启动自动运动。
void Menu_Init(uint32_t now_ms);

// 函    数：Menu_Update
// 参    数：now_ms 当前系统毫秒时间戳；key_events KeyScan_GetEvents() 读取并清除后的事件位图。
// 返 回 值：无
// 注意事项：非阻塞轮询接口；需要在主循环中周期调用，不能在中断中调用。
void Menu_Update(uint32_t now_ms, uint16_t key_events);

#endif
