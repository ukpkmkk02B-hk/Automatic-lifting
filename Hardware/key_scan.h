#ifndef __KEY_SCAN_H
#define __KEY_SCAN_H

#include "stm32f10x.h"

// 模    块：非阻塞按键扫描模块
// 硬件假设：PB1/PB11/PB10/PB0 为低有效输入，使用 MCU 上拉。
// 时间单位：所有按键时间均为 ms；25ms 消抖，25-1000ms 短按，>=1000ms 长按。
// 长按事件：PB1/PB11 达到 1000ms 时立即上报一次，便于手动点动；其他普通长按在释放沿确认。
// 特殊事件：PB10 >=3000ms 额外产生维护入口事件，且释放时不再重复上报普通长按。

typedef enum
{
    // PB1：菜单减小或手动下降，由上层按当前页面解释。
    KEY_SCAN_KEY1 = 0,
    // PB11：菜单增加或手动上升，由上层按当前页面解释。
    KEY_SCAN_KEY2,
    // PB10：暂停、确认、报警静音和维护入口。
    KEY_SCAN_PAUSE_CONFIRM,
    // PB0：页面切换或取消。
    KEY_SCAN_PAGE_MENU,
    KEY_SCAN_COUNT
} KeyScan_Key_t;

// 事件位在 KeyScan_GetEvents() 读取后清零，避免同一次短按被重复消费。
#define KEY_SCAN_EVENT_KEY1_SHORT        0x0001U
#define KEY_SCAN_EVENT_KEY2_SHORT        0x0002U
#define KEY_SCAN_EVENT_PAUSE_SHORT       0x0004U
#define KEY_SCAN_EVENT_PAGE_SHORT        0x0008U
#define KEY_SCAN_EVENT_KEY1_LONG         0x0010U
#define KEY_SCAN_EVENT_KEY2_LONG         0x0020U
#define KEY_SCAN_EVENT_PAUSE_LONG        0x0040U
#define KEY_SCAN_EVENT_PAGE_LONG         0x0080U
#define KEY_SCAN_EVENT_MAINTENANCE_ENTRY 0x0100U

// 函    数：KeyScan_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化四路按键 GPIO，并记录初始按下状态；不会阻塞等待按键释放。
void KeyScan_Init(void);

// 函    数：KeyScan_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：按 BOARD_KEY_SCAN_PERIOD_MS 周期扫描；PB1/PB11 长按保持期间上报一次，短按在释放时确认。
void KeyScan_Update(uint32_t now_ms);

// 函    数：KeyScan_GetEvents
// 参    数：无
// 返 回 值：按键事件位图，可能同时包含多个事件。
// 注意事项：读取后清空事件缓存，上层应及时消费。
uint16_t KeyScan_GetEvents(void);

// 函    数：KeyScan_IsPressed
// 参    数：key 按键编号，范围为 KeyScan_Key_t。
// 返 回 值：1 表示滤波后按下，0 表示松开或参数非法。
// 注意事项：返回的是稳定状态，不是原始 GPIO 瞬时电平。
uint8_t KeyScan_IsPressed(KeyScan_Key_t key);

#endif
