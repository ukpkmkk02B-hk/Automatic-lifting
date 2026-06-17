#ifndef __AUTO_CONTROL_H
#define __AUTO_CONTROL_H

#include "stm32f10x.h"
#include "error_code.h"
#include "nap_scheduler.h"
#include "stepper_um244.h"
#include "water_depth.h"

// 模    块：自动控制仲裁器
// 职    责：在自动模式中按优先级选择快速掉水跟随、低频闭环修正、每日打盹或等待。
// 安全假设：本模块不直接输出 STEP，不操作 GPIO；限位、传感器、I2C、位置可信和电机释放由 app_state 先检查。

typedef enum
{
	// 无运动请求，保持等待或显示。
	AUTO_CONTROL_DECISION_NONE = 0,
	// 请求 app_state 通过统一有限脉冲接口启动一次运动。
	AUTO_CONTROL_DECISION_MOVE,
	// 请求进入暂停，通常用于 DROP 结束后等待人工确认。
	AUTO_CONTROL_DECISION_PAUSE,
	// 请求进入故障，error_code 指明原因。
	AUTO_CONTROL_DECISION_FAULT
} AutoControl_DecisionType_t;

typedef enum
{
	// 普通自动等待或每日打盹。
	AUTO_CONTROL_DISPLAY_AUTO = 0,
	// 低频闭环水深修正。
	AUTO_CONTROL_DISPLAY_TRACK,
	// 快速掉水跟随。
	AUTO_CONTROL_DISPLAY_DROP
} AutoControl_Display_t;

typedef struct
{
	WaterDepth_State_t depth;
	int32_t target_depth_mm_x10;
	uint8_t nap_due;
	uint16_t nap_pulses;
	uint32_t daily_remaining_pulses;
} AutoControl_Input_t;

typedef struct
{
	AutoControl_DecisionType_t type;
	StepperUM244_Direction_t direction;
	uint16_t pulses;
	uint16_t frequency_hz;
	MotionSource_t source;
	ErrorCode_t error_code;
	AutoControl_Display_t display;
	uint8_t check_water_notice;
} AutoControl_Decision_t;

// 函    数：AutoControl_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：初始化低频闭环和快速掉水跟随内部状态，不启动运动。
void AutoControl_Init(uint32_t now_ms);

// 函    数：AutoControl_Reset
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：进入自动或人工确认后调用，清除 DROP/TRK 临时状态并重新排程低频检查。
void AutoControl_Reset(uint32_t now_ms);

// 函    数：AutoControl_Arbitrate
// 参    数：now_ms 当前时间；input 自动控制输入；decision 输出仲裁结果。
// 返 回 值：无
// 注意事项：只做决策，不调用 StepperUM244，不写 Flash。
void AutoControl_Arbitrate(uint32_t now_ms,
                           const AutoControl_Input_t *input,
                           AutoControl_Decision_t *decision);

// 函    数：AutoControl_NotifyMoveComplete
// 参    数：source 运动来源；direction 方向；pulses 实际完成 pulse；before/after 运动前后水深；now_ms 当前时间。
// 返 回 值：无
// 注意事项：用于更新低频闭环稳定等待、DROP 累计距离和后续仲裁状态。
void AutoControl_NotifyMoveComplete(MotionSource_t source,
                                    StepperUM244_Direction_t direction,
                                    uint16_t pulses,
                                    const WaterDepth_State_t *before,
                                    const WaterDepth_State_t *after,
                                    uint32_t now_ms);

// 函    数：AutoControl_NotifyMoveAbort
// 参    数：source 被中止的运动来源；now_ms 当前时间。
// 返 回 值：无
// 注意事项：运动被故障、暂停或抢占中止时调用，避免内部状态误认为仍在稳定等待。
void AutoControl_NotifyMoveAbort(MotionSource_t source, uint32_t now_ms);

// 函    数：AutoControl_GetDisplay
// 参    数：无
// 返 回 值：当前自动控制显示状态。
AutoControl_Display_t AutoControl_GetDisplay(void);

// 函    数：AutoControl_GetMotionText
// 参    数：无
// 返 回 值：4 字符以内 ASCII 文本，用于 OLED 主页面运动字段。
const char *AutoControl_GetMotionText(void);

#endif
