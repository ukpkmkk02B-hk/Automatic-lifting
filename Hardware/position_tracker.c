#include "position_tracker.h"
#include "board_config.h"
#include "error_manager.h"
#include "limit.h"

static int32_t s_position_pulses;
static uint8_t s_position_trusted;
static PositionTracker_UntrustedReason_t s_untrusted_reason;

static int32_t PositionTracker_MaxPulses(void)
{
	return (int32_t)BOARD_BASKET_MAX_POSITION_PULSES;
}

static void PositionTracker_SetUntrusted(PositionTracker_UntrustedReason_t reason)
{
	s_position_trusted = 0U;
	s_untrusted_reason = reason;
}

void PositionTracker_Init(void)
{
	s_position_pulses = 0L;
	s_position_trusted = 0U;
	s_untrusted_reason = POSITION_TRACKER_UNTRUSTED_NOT_HOMED;
}

void PositionTracker_MarkHomed(void)
{
	s_position_pulses = 0L;
	s_position_trusted = 1U;
	s_untrusted_reason = POSITION_TRACKER_UNTRUSTED_NONE;
	ErrorManager_Clear(ERROR_CODE_E_POSITION_UNTRUSTED);
}

void PositionTracker_MarkUntrusted(PositionTracker_UntrustedReason_t reason)
{
	if (reason == POSITION_TRACKER_UNTRUSTED_NONE)
	{
		reason = POSITION_TRACKER_UNTRUSTED_NOT_HOMED;
	}

	PositionTracker_SetUntrusted(reason);
	ErrorManager_Set(ERROR_CODE_E_POSITION_UNTRUSTED);
}

uint8_t PositionTracker_IsTrusted(void)
{
	return s_position_trusted;
}

uint8_t PositionTracker_CanAutoRun(void)
{
	if (s_position_trusted == 0U)
	{
		return 0U;
	}

	if ((s_position_pulses < 0L) ||
	    (s_position_pulses > PositionTracker_MaxPulses()))
	{
		return 0U;
	}

	return 1U;
}

int32_t PositionTracker_GetPulses(void)
{
	return s_position_pulses;
}

int32_t PositionTracker_GetMmX10(void)
{
	return (int32_t)(((int64_t)s_position_pulses * 10LL) /
	                 (int64_t)BOARD_STEPPER_PULSE_PER_MM);
}

PositionTracker_UntrustedReason_t PositionTracker_GetUntrustedReason(void)
{
	return s_untrusted_reason;
}

void PositionTracker_ApplyCompletedMove(StepperUM244_Direction_t direction, uint16_t pulses)
{
	int32_t delta;
	int32_t next_position;

	if (s_position_trusted == 0U)
	{
		return;
	}

	delta = (int32_t)pulses;
	if (direction == STEPPER_UM244_DIRECTION_DOWN)
	{
		delta = -delta;
	}

	next_position = s_position_pulses + delta;
	if ((next_position < 0L) || (next_position > PositionTracker_MaxPulses()))
	{
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_RANGE);
		return;
	}

	s_position_pulses = next_position;
}

void PositionTracker_ServiceSafety(void)
{
	if (StepperUM244_IsMotorReleased() != 0U)
	{
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_MOTOR_RELEASED);
	}

	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_LIMIT_MISMATCH);
	}

	if (ErrorManager_IsActive(ERROR_CODE_E_STALL) != 0U)
	{
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_STALL);
	}
}
