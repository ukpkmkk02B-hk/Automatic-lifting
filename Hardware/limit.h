#ifndef __LIMIT_H
#define __LIMIT_H

#include "stm32f10x.h"

// 模    块：限位输入模块
// 硬件假设：四个 24V NPN 限位开关通过光耦隔离进入 PB12-PB15。
// 有效电平：MCU 侧上拉，GPIO 读到低电平表示物理限位已触发。
// 安全约束：上升必须检查两个上限位，下降必须检查两个下限位；
//           左右同方向限位不一致时，上层应停止所有运动并报警。

typedef enum
{
    // 左侧上限位，限制框篮继续上升。
    LIMIT_LEFT_UPPER = 0,
    // 左侧下限位，限制框篮继续下降。
    LIMIT_LEFT_LOWER,
    // 右侧上限位，限制框篮继续上升。
    LIMIT_RIGHT_UPPER,
    // 右侧下限位，限制框篮继续下降。
    LIMIT_RIGHT_LOWER,
    LIMIT_COUNT
} Limit_Channel_t;

typedef enum
{
    // 上升方向：框篮向上，basket_depth_mm 变小。
    LIMIT_DIRECTION_UP = 0,
    // 下降方向：框篮向下，basket_depth_mm 变大。
    LIMIT_DIRECTION_DOWN
} Limit_Direction_t;

typedef struct
{
    // 字段值为 1 表示已触发，0 表示未触发；该状态为滤波后的稳定值。
    uint8_t left_upper;
    uint8_t left_lower;
    uint8_t right_upper;
    uint8_t right_lower;
} Limit_State_t;

// 函    数：Limit_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化四路限位 GPIO 为上拉输入，并读取当前物理状态作为初始滤波值。
void Limit_Init(void);

// 函    数：Limit_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：按 BOARD_LIMIT_SAMPLE_MS 周期轮询并滤波；触发确认短于释放确认以便尽快停机。
void Limit_Update(uint32_t now_ms);

// 函    数：Limit_ReadRaw
// 参    数：channel 限位通道，范围为 Limit_Channel_t。
// 返 回 值：1 表示原始 GPIO 已触发，0 表示未触发；非法通道按已触发返回。
// 注意事项：用于 STEP 中断急停路径，不等待滤波确认。
uint8_t Limit_ReadRaw(Limit_Channel_t channel);

// 函    数：Limit_IsActive
// 参    数：channel 限位通道，范围为 Limit_Channel_t。
// 返 回 值：1 表示滤波后已触发，0 表示未触发；非法通道按已触发返回。
// 注意事项：用于主循环显示和一般安全判断，不替代中断急停原始读数。
uint8_t Limit_IsActive(Limit_Channel_t channel);

// 函    数：Limit_GetState
// 参    数：state 输出四路滤波状态的结构体指针，允许为 NULL。
// 返 回 值：无
// 注意事项：输出值均为 1=触发、0=未触发。
void Limit_GetState(Limit_State_t *state);

// 函    数：Limit_IsAnyUpperActive
// 参    数：无
// 返 回 值：任意上限位触发时返回 1。
// 注意事项：返回 1 时上升运动必须禁止。
uint8_t Limit_IsAnyUpperActive(void);

// 函    数：Limit_IsAnyLowerActive
// 参    数：无
// 返 回 值：任意下限位触发时返回 1。
// 注意事项：返回 1 时下降运动必须禁止。
uint8_t Limit_IsAnyLowerActive(void);

// 函    数：Limit_IsUpperMismatch
// 参    数：无
// 返 回 值：左右上限位滤波状态不一致时返回 1。
// 注意事项：表示两侧机械或传感器可能不同步，上层应停止运动。
uint8_t Limit_IsUpperMismatch(void);

// 函    数：Limit_IsLowerMismatch
// 参    数：无
// 返 回 值：左右下限位滤波状态不一致时返回 1。
// 注意事项：表示两侧机械或传感器可能不同步，上层应停止运动。
uint8_t Limit_IsLowerMismatch(void);

// 函    数：Limit_IsSameDirectionMismatch
// 参    数：无
// 返 回 值：任一同方向左右限位不一致时返回 1。
// 注意事项：返回 1 时上层应停止全部运动并置位故障。
uint8_t Limit_IsSameDirectionMismatch(void);

// 函    数：Limit_IsDirectionBlocked
// 参    数：direction 运动方向，上升检查上限位，下降检查下限位。
// 返 回 值：1 表示该方向被滤波后的限位状态禁止，0 表示未禁止。
// 注意事项：用于命令启动前的安全判断。
uint8_t Limit_IsDirectionBlocked(Limit_Direction_t direction);

// 函    数：Limit_IsRawDirectionActive
// 参    数：direction 运动方向，上升直读上限位，下降直读下限位。
// 返 回 值：1 表示该方向原始 GPIO 已触发，0 表示未触发。
// 注意事项：用于中断内急停，不等待滤波确认。
uint8_t Limit_IsRawDirectionActive(Limit_Direction_t direction);

#endif
