#ifndef __HOMING_H
#define __HOMING_H

#include "stm32f10x.h"

// 模    块：维护回零状态机
// 回零流程：低速下降找下限位 -> 上升 1mm 释放限位 -> 再次下降找下限位 -> 记为 0mm。
// 安全约束：回零不在开机时自动执行；回零中仍然遵守限位不一致、上限位、
//           传感器/I2C/水深等阻塞故障。

typedef enum
{
	// 回零命令或状态正常。
	HOMING_STATUS_OK = 0,
	// 回零正在执行。
	HOMING_STATUS_BUSY,
	// 参数或内部状态非法。
	HOMING_STATUS_ERROR_PARAM,
	// 限位相关错误。
	HOMING_STATUS_ERROR_LIMIT,
	// 存在阻塞回零的严重故障。
	HOMING_STATUS_ERROR_FAULT,
	// 搜索超过最大允许脉冲仍未找到下限位。
	HOMING_STATUS_ERROR_TIMEOUT,
	// 用户或上层取消回零。
	HOMING_STATUS_CANCELLED
} Homing_Status_t;

typedef enum
{
	// 空闲，尚未开始回零。
	HOMING_STATE_IDLE = 0,
	// 准备第一次向下搜索下限位。
	HOMING_STATE_START_DOWN_FIRST,
	// 第一次向下运动中。
	HOMING_STATE_DOWN_FIRST,
	// 准备向上退 1mm 释放下限位。
	HOMING_STATE_START_BACKOFF,
	// 向上退限位中。
	HOMING_STATE_BACKOFF_UP,
	// 等待下限位释放确认。
	HOMING_STATE_WAIT_RELEASE,
	// 准备第二次向下搜索下限位。
	HOMING_STATE_START_DOWN_SECOND,
	// 第二次向下运动中。
	HOMING_STATE_DOWN_SECOND,
	// 回零完成，位置可信并归零。
	HOMING_STATE_COMPLETE,
	// 回零故障。
	HOMING_STATE_FAULT,
	// 回零已取消。
	HOMING_STATE_CANCELLED
} Homing_State_t;

// 函    数：Homing_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化回零状态机为空闲。
void Homing_Init(void);

// 函    数：Homing_Start
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：回零启动状态。
// 注意事项：启动维护回零流程；开始后位置立即标记为不可信，直到二次触发下限位完成。
Homing_Status_t Homing_Start(uint32_t now_ms);

// 函    数：Homing_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：非阻塞推进回零状态机，主循环周期性调用。
void Homing_Update(uint32_t now_ms);

// 函    数：Homing_Cancel
// 参    数：无
// 返 回 值：无
// 注意事项：取消正在执行的回零，并标记位置不可信。
void Homing_Cancel(void);

// 函    数：Homing_IsBusy
// 参    数：无
// 返 回 值：1 表示回零正在执行，0 表示空闲/完成/故障/取消。
// 注意事项：供上层避免重复启动回零。
uint8_t Homing_IsBusy(void);

// 函    数：Homing_IsComplete
// 参    数：无
// 返 回 值：1 表示回零已完成，0 表示未完成。
// 注意事项：完成后位置跟踪模块应已标记为 0mm 且可信。
uint8_t Homing_IsComplete(void);

// 函    数：Homing_GetState
// 参    数：无
// 返 回 值：当前回零状态。
// 注意事项：用于 OLED 或调试页面显示状态机阶段。
Homing_State_t Homing_GetState(void);

// 函    数：Homing_GetLastStatus
// 参    数：无
// 返 回 值：最近一次回零结果。
// 注意事项：故障或取消后用于上层显示原因。
Homing_Status_t Homing_GetLastStatus(void);

// 函    数：Homing_GetSearchPulses
// 参    数：无
// 返 回 值：本次回零累计搜索脉冲数。
// 注意事项：用于诊断行程或限位异常。
uint32_t Homing_GetSearchPulses(void);

#endif
