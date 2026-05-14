#ifndef __POSITION_TRACKER_H
#define __POSITION_TRACKER_H

#include "stm32f10x.h"
#include "stepper_um244.h"

// 模    块：机械位置跟踪
// 位置单位：内部单位为 pulse；最低机械位置为 0 pulse / 0mm，向上为正方向。
// 安全约束：未回零、释放电机、限位不一致、卡滞或越界后，位置都不可信；
//           不可信位置禁止自动运行。

typedef enum
{
	// 位置可信，没有不可信原因。
	POSITION_TRACKER_UNTRUSTED_NONE = 0,
	// 上电后尚未执行维护回零。
	POSITION_TRACKER_UNTRUSTED_NOT_HOMED,
	// 回零流程已开始，完成前不能自动运行。
	POSITION_TRACKER_UNTRUSTED_HOMING_STARTED,
	// 电机被 MF 释放，机械位置可能被人工移动。
	POSITION_TRACKER_UNTRUSTED_MOTOR_RELEASED,
	// 左右限位不一致，机械同步关系不可靠。
	POSITION_TRACKER_UNTRUSTED_LIMIT_MISMATCH,
	// 卡滞故障后位置估算不可靠。
	POSITION_TRACKER_UNTRUSTED_STALL,
	// 累计位置超出 0..最大行程范围。
	POSITION_TRACKER_UNTRUSTED_RANGE
} PositionTracker_UntrustedReason_t;

// 函    数：PositionTracker_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化为未回零、不可信位置。
void PositionTracker_Init(void);

// 函    数：PositionTracker_MarkHomed
// 参    数：无
// 返 回 值：无
// 注意事项：回零完成后调用，把当前位置设为 0 pulse，并清除位置不可信错误。
void PositionTracker_MarkHomed(void);

// 函    数：PositionTracker_Restore
// 参    数：pulses Flash 保存的机械位置，单位 pulse；trusted 非 0 表示保存时位置可信。
// 返 回 值：1 表示恢复为可信位置，0 表示保持或标记为不可信。
// 注意事项：仅供开机自检后的断电恢复流程调用；越界位置会置位 E_POSITION_UNTRUSTED。
uint8_t PositionTracker_Restore(int32_t pulses, uint8_t trusted);

// 函    数：PositionTracker_MarkUntrusted
// 参    数：reason 位置不可信原因。
// 返 回 值：无
// 注意事项：标记位置不可信，并置位 E_POSITION_UNTRUSTED。
void PositionTracker_MarkUntrusted(PositionTracker_UntrustedReason_t reason);

// 函    数：PositionTracker_IsTrusted
// 参    数：无
// 返 回 值：1 表示位置可信，0 表示位置不可信。
// 注意事项：只返回可信标志，不判断位置是否越界。
uint8_t PositionTracker_IsTrusted(void);

// 函    数：PositionTracker_CanAutoRun
// 参    数：无
// 返 回 值：1 表示位置可信且处于 0..最大行程范围内，0 表示禁止自动运行。
// 注意事项：防止断电后或释放电机后追赶误动作。
uint8_t PositionTracker_CanAutoRun(void);

// 函    数：PositionTracker_GetPulses
// 参    数：无
// 返 回 值：当前位置，单位 pulse。
// 注意事项：调用方仍需检查位置可信标志。
int32_t PositionTracker_GetPulses(void);

// 函    数：PositionTracker_GetMmX10
// 参    数：无
// 返 回 值：当前位置，单位 mm_x10。
// 注意事项：pulse -> mm_x10 按 800 pulse/mm 整数换算。
int32_t PositionTracker_GetMmX10(void);

// 函    数：PositionTracker_GetUntrustedReason
// 参    数：无
// 返 回 值：最近一次位置不可信原因。
// 注意事项：位置可信时返回 POSITION_TRACKER_UNTRUSTED_NONE。
PositionTracker_UntrustedReason_t PositionTracker_GetUntrustedReason(void);

// 函    数：PositionTracker_ApplyCompletedMove
// 参    数：direction 已完成运动方向；pulses 已完成有效 STEP 脉冲数。
// 返 回 值：无
// 注意事项：仅在有限脉冲正常完成后更新估算位置；不可信位置下不会继续累计。
void PositionTracker_ApplyCompletedMove(StepperUM244_Direction_t direction, uint16_t pulses);

// 函    数：PositionTracker_ServiceSafety
// 参    数：无
// 返 回 值：无
// 注意事项：周期性检查释放电机、限位不一致和卡滞故障对位置可信度的影响。
void PositionTracker_ServiceSafety(void);

#endif
