#include "position_tracker.h"
#include "board_config.h"
#include "error_manager.h"
#include "limit.h"

static int32_t s_position_pulses;
static uint8_t s_position_trusted;
static PositionTracker_UntrustedReason_t s_untrusted_reason;

// 函    数：PositionTracker_MaxPulses
// 参    数：无
// 返 回 值：机械最大行程对应的位置上限，单位 pulse。
// 注意事项：最大行程 170mm，按 800 pulse/mm 换算。
static int32_t PositionTracker_MaxPulses(void)
{
	return (int32_t)BOARD_BASKET_MAX_POSITION_PULSES;
}

// 函    数：PositionTracker_SetUntrusted
// 参    数：reason 位置不可信原因。
// 返 回 值：无
// 注意事项：只更新本模块标志，不直接置位错误码；外层接口负责故障锁存。
static void PositionTracker_SetUntrusted(PositionTracker_UntrustedReason_t reason)
{
	s_position_trusted = 0U;
	s_untrusted_reason = reason;
}

// 函    数：PositionTracker_Init
// 参    数：无
// 返 回 值：无
// 注意事项：上电后默认未回零，位置不可信，禁止自动运行。
void PositionTracker_Init(void)
{
	s_position_pulses = 0L;
	s_position_trusted = 0U;
	s_untrusted_reason = POSITION_TRACKER_UNTRUSTED_NOT_HOMED;
}

// 函    数：PositionTracker_MarkHomed
// 参    数：无
// 返 回 值：无
// 注意事项：维护回零二次触发下限位后，把最低机械点定义为 0。
void PositionTracker_MarkHomed(void)
{
	s_position_pulses = 0L;
	s_position_trusted = 1U;
	s_untrusted_reason = POSITION_TRACKER_UNTRUSTED_NONE;
	ErrorManager_Clear(ERROR_CODE_E_POSITION_UNTRUSTED);
}

// 函    数：PositionTracker_Restore
// 参    数：pulses Flash 保存位置，单位 pulse；trusted 保存时位置可信标志。
// 返 回 值：恢复可信返回 1，否则返回 0。
// 注意事项：断电恢复只接受 0..最大行程内的可信位置，避免重启后追赶未知位移。
uint8_t PositionTracker_Restore(int32_t pulses, uint8_t trusted)
{
	if (trusted == 0U)
	{
		PositionTracker_SetUntrusted(POSITION_TRACKER_UNTRUSTED_NOT_HOMED);
		ErrorManager_Set(ERROR_CODE_E_POSITION_UNTRUSTED);
		return 0U;
	}

	if ((pulses < 0L) || (pulses > PositionTracker_MaxPulses()))
	{
		s_position_pulses = 0L;
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_RANGE);
		return 0U;
	}

	s_position_pulses = pulses;
	s_position_trusted = 1U;
	s_untrusted_reason = POSITION_TRACKER_UNTRUSTED_NONE;
	ErrorManager_Clear(ERROR_CODE_E_POSITION_UNTRUSTED);
	return 1U;
}

// 函    数：PositionTracker_MarkUntrusted
// 参    数：reason 位置不可信原因。
// 返 回 值：无
// 注意事项：置位 E_POSITION_UNTRUSTED，自动运行必须等待回零或人工恢复后再允许。
void PositionTracker_MarkUntrusted(PositionTracker_UntrustedReason_t reason)
{
	if (reason == POSITION_TRACKER_UNTRUSTED_NONE)
	{
		// 调用方传入 NONE 时，降级为未回零，避免出现“不可信但无原因”。
		reason = POSITION_TRACKER_UNTRUSTED_NOT_HOMED;
	}

	PositionTracker_SetUntrusted(reason);
	ErrorManager_Set(ERROR_CODE_E_POSITION_UNTRUSTED);
}

// 函    数：PositionTracker_IsTrusted
// 参    数：无
// 返 回 值：1 表示位置可信，0 表示不可信。
// 注意事项：可信不等于允许自动运行，自动运行还要检查行程范围。
uint8_t PositionTracker_IsTrusted(void)
{
	return s_position_trusted;
}

// 函    数：PositionTracker_CanAutoRun
// 参    数：无
// 返 回 值：1 表示可自动运行，0 表示禁止自动运行。
// 注意事项：未回零或不可信位置不允许自动运行，避免断电后追赶误动作。
uint8_t PositionTracker_CanAutoRun(void)
{
	if (s_position_trusted == 0U)
	{
		// 未回零或不可信位置不允许自动运行，避免断电后追赶误动作。
		return 0U;
	}

	if ((s_position_pulses < 0L) ||
	    (s_position_pulses > PositionTracker_MaxPulses()))
	{
		return 0U;
	}

	return 1U;
}

// 函    数：PositionTracker_GetPulses
// 参    数：无
// 返 回 值：当前位置，单位 pulse。
// 注意事项：调用方仍需检查 PositionTracker_IsTrusted()。
int32_t PositionTracker_GetPulses(void)
{
	return s_position_pulses;
}

// 函    数：PositionTracker_GetMmX10
// 参    数：无
// 返 回 值：当前位置，单位 mm_x10。
// 注意事项：pulse -> mm_x10：pulse * 10 / 800，不使用浮点。
int32_t PositionTracker_GetMmX10(void)
{
	return (int32_t)(((int64_t)s_position_pulses * 10LL) /
	                 (int64_t)BOARD_STEPPER_PULSE_PER_MM);
}

// 函    数：PositionTracker_GetUntrustedReason
// 参    数：无
// 返 回 值：最近一次位置不可信原因。
// 注意事项：用于 OLED 或诊断页面解释为什么不能自动运行。
PositionTracker_UntrustedReason_t PositionTracker_GetUntrustedReason(void)
{
	return s_untrusted_reason;
}

// 函    数：PositionTracker_ApplyCompletedMove
// 参    数：direction 已完成运动方向；pulses 已完成有效 STEP 脉冲数。
// 返 回 值：无
// 注意事项：只有位置可信时才累计估算；超出 0..最大行程会立即标记不可信。
void PositionTracker_ApplyCompletedMove(StepperUM244_Direction_t direction, uint16_t pulses)
{
	int32_t delta;
	int32_t next_position;

	if (s_position_trusted == 0U)
	{
		// 不可信位置下不继续累计估算，等待重新回零。
		return;
	}

	delta = (int32_t)pulses;
	if (direction == STEPPER_UM244_DIRECTION_DOWN)
	{
		// 内部约定向上为正，下降会让当前位置 pulse 减小。
		delta = -delta;
	}

	next_position = s_position_pulses + delta;
	if ((next_position < 0L) || (next_position > PositionTracker_MaxPulses()))
	{
		// 位置超出机械范围时停止信任估算值。
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_RANGE);
		return;
	}

	s_position_pulses = next_position;
}

// 函    数：PositionTracker_ServiceSafety
// 参    数：无
// 返 回 值：无
// 注意事项：主循环周期性调用，遇到释放电机、限位不一致或卡滞时标记位置不可信。
void PositionTracker_ServiceSafety(void)
{
	if (StepperUM244_IsMotorReleased() != 0U)
	{
		// MF 释放后人工或重力可能改变位置，必须重新回零。
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_MOTOR_RELEASED);
	}

	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		// 两侧限位状态不一致时，位置估算不再代表两侧真实机械状态。
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_LIMIT_MISMATCH);
	}

	if (ErrorManager_IsActive(ERROR_CODE_E_STALL) != 0U)
	{
		// 卡滞说明脉冲与实际水深变化不匹配，位置累计也不可信。
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_STALL);
	}
}
