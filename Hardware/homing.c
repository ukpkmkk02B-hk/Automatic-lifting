#include "homing.h"
#include "board_config.h"
#include "error_manager.h"
#include "limit.h"
#include "position_tracker.h"
#include "stepper_um244.h"

static Homing_State_t s_state;
static Homing_Status_t s_last_status;
static uint32_t s_phase_search_pulses;
static uint32_t s_total_search_pulses;
static uint32_t s_release_deadline_ms;

static uint8_t Homing_TimeElapsed(uint32_t now_ms, uint32_t deadline_ms)
{
	return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1U : 0U;
}

static uint8_t Homing_IsBlockingFaultActive(void)
{
	if ((ErrorManager_IsActive(ERROR_CODE_E_SENSOR_AIR_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_SENSOR_TANK_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_SENSOR_BASKET_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_I2C_A_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_I2C_B_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_I2C_C_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_TANK_LOW) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_TANK_HIGH) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_BASKET_LOW) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_BASKET_HIGH) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_WATER_JUMP) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_PRESSURE_PHYSICAL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_STALL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_SELF_TEST_FAIL) != 0U) ||
	    (ErrorManager_IsActive(ERROR_CODE_E_MOTOR_RELEASED) != 0U))
	{
		return 1U;
	}

	return 0U;
}

static void Homing_Fail(Homing_Status_t status, ErrorCode_t code)
{
	StepperUM244_Stop();
	if (code != ERROR_CODE_E_NONE)
	{
		ErrorManager_Set(code);
	}
	PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_NOT_HOMED);
	s_last_status = status;
	s_state = HOMING_STATE_FAULT;
}

static uint8_t Homing_CheckSafety(void)
{
	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LIMIT_MISMATCH);
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_LIMIT_MISMATCH);
		return 0U;
	}

	if (Limit_IsAnyUpperActive() != 0U)
	{
		Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_UPPER_LIMIT);
		return 0U;
	}

	if (Homing_IsBlockingFaultActive() != 0U)
	{
		Homing_Fail(HOMING_STATUS_ERROR_FAULT, ERROR_CODE_E_NONE);
		return 0U;
	}

	return 1U;
}

static Homing_Status_t Homing_StartDownChunk(uint32_t now_ms)
{
	StepperUM244_Status_t status;

	status = StepperUM244_StartUntilLimit(STEPPER_UM244_DIRECTION_DOWN,
	                                     BOARD_HOMING_SEARCH_CHUNK_PULSES,
	                                     BOARD_STEPPER_HOMING_FREQ_HZ,
	                                     now_ms);
	if (status == STEPPER_UM244_STATUS_OK)
	{
		return HOMING_STATUS_OK;
	}
	if (status == STEPPER_UM244_STATUS_BUSY)
	{
		return HOMING_STATUS_BUSY;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_LIMIT)
	{
		return HOMING_STATUS_ERROR_LIMIT;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_FAULT)
	{
		return HOMING_STATUS_ERROR_FAULT;
	}

	return HOMING_STATUS_ERROR_PARAM;
}

static Homing_Status_t Homing_StartBackoff(uint32_t now_ms)
{
	StepperUM244_Status_t status;

	status = StepperUM244_StartPulses(STEPPER_UM244_DIRECTION_UP,
	                                  BOARD_HOMING_BACKOFF_PULSES,
	                                  BOARD_STEPPER_HOMING_FREQ_HZ,
	                                  now_ms);
	if (status == STEPPER_UM244_STATUS_OK)
	{
		return HOMING_STATUS_OK;
	}
	if (status == STEPPER_UM244_STATUS_BUSY)
	{
		return HOMING_STATUS_BUSY;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_LIMIT)
	{
		return HOMING_STATUS_ERROR_LIMIT;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_FAULT)
	{
		return HOMING_STATUS_ERROR_FAULT;
	}

	return HOMING_STATUS_ERROR_PARAM;
}

static void Homing_HandleDownComplete(Homing_State_t next_when_limit)
{
	StepperUM244_StopReason_t reason;

	reason = StepperUM244_GetStopReason();
	if (reason == STEPPER_UM244_STOP_EXPECTED_LIMIT)
	{
		s_phase_search_pulses = 0U;
		s_state = next_when_limit;
		return;
	}

	if (reason == STEPPER_UM244_STOP_PULSE_DONE)
	{
		s_phase_search_pulses += StepperUM244_GetCompletedPulses();
		s_total_search_pulses += StepperUM244_GetCompletedPulses();
		if ((s_phase_search_pulses >= BOARD_HOMING_MAX_SEARCH_PULSES) ||
		    (s_total_search_pulses >= (BOARD_HOMING_MAX_SEARCH_PULSES * 2U)))
		{
			Homing_Fail(HOMING_STATUS_ERROR_TIMEOUT, ERROR_CODE_E_POSITION_UNTRUSTED);
		}
		else
		{
			s_state = (next_when_limit == HOMING_STATE_START_BACKOFF) ?
			          HOMING_STATE_START_DOWN_FIRST :
			          HOMING_STATE_START_DOWN_SECOND;
		}
		return;
	}

	if (reason == STEPPER_UM244_STOP_MISMATCH_FAULT)
	{
		Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LIMIT_MISMATCH);
		return;
	}

	Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LOWER_LIMIT);
}

void Homing_Init(void)
{
	s_state = HOMING_STATE_IDLE;
	s_last_status = HOMING_STATUS_OK;
	s_phase_search_pulses = 0U;
	s_total_search_pulses = 0U;
	s_release_deadline_ms = 0U;
}

Homing_Status_t Homing_Start(uint32_t now_ms)
{
	if ((s_state != HOMING_STATE_IDLE) &&
	    (s_state != HOMING_STATE_COMPLETE) &&
	    (s_state != HOMING_STATE_FAULT) &&
	    (s_state != HOMING_STATE_CANCELLED))
	{
		return HOMING_STATUS_BUSY;
	}

	StepperUM244_SetMotorRelease(0U);
	PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_HOMING_STARTED);

	s_phase_search_pulses = 0U;
	s_total_search_pulses = 0U;
	s_release_deadline_ms = now_ms;
	s_last_status = HOMING_STATUS_BUSY;

	if (Homing_CheckSafety() == 0U)
	{
		return s_last_status;
	}

	if (Limit_IsAnyLowerActive() != 0U)
	{
		s_state = HOMING_STATE_START_BACKOFF;
	}
	else
	{
		s_state = HOMING_STATE_START_DOWN_FIRST;
	}

	return HOMING_STATUS_OK;
}

void Homing_Update(uint32_t now_ms)
{
	Homing_Status_t status;

	if ((s_state == HOMING_STATE_IDLE) ||
	    (s_state == HOMING_STATE_COMPLETE) ||
	    (s_state == HOMING_STATE_FAULT) ||
	    (s_state == HOMING_STATE_CANCELLED))
	{
		return;
	}

	if (Homing_CheckSafety() == 0U)
	{
		return;
	}

	switch (s_state)
	{
	case HOMING_STATE_START_DOWN_FIRST:
		status = Homing_StartDownChunk(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_DOWN_FIRST;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_LOWER_LIMIT);
		}
		break;

	case HOMING_STATE_DOWN_FIRST:
		if (StepperUM244_IsBusy() == 0U)
		{
			Homing_HandleDownComplete(HOMING_STATE_START_BACKOFF);
		}
		break;

	case HOMING_STATE_START_BACKOFF:
		status = Homing_StartBackoff(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_BACKOFF_UP;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_UPPER_LIMIT);
		}
		break;

	case HOMING_STATE_BACKOFF_UP:
		if (StepperUM244_IsBusy() == 0U)
		{
			if (StepperUM244_GetStopReason() != STEPPER_UM244_STOP_PULSE_DONE)
			{
				Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_UPPER_LIMIT);
			}
			else
			{
				s_release_deadline_ms = now_ms + BOARD_HOMING_RELEASE_WAIT_MS;
				s_state = HOMING_STATE_WAIT_RELEASE;
			}
		}
		break;

	case HOMING_STATE_WAIT_RELEASE:
		if (Limit_IsAnyLowerActive() == 0U)
		{
			s_phase_search_pulses = 0U;
			s_state = HOMING_STATE_START_DOWN_SECOND;
		}
		else if (Homing_TimeElapsed(now_ms, s_release_deadline_ms) != 0U)
		{
			Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LOWER_LIMIT);
		}
		else
		{
		}
		break;

	case HOMING_STATE_START_DOWN_SECOND:
		status = Homing_StartDownChunk(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_DOWN_SECOND;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_LOWER_LIMIT);
		}
		break;

	case HOMING_STATE_DOWN_SECOND:
		if (StepperUM244_IsBusy() == 0U)
		{
			Homing_HandleDownComplete(HOMING_STATE_COMPLETE);
			if (s_state == HOMING_STATE_COMPLETE)
			{
				PositionTracker_MarkHomed();
				s_last_status = HOMING_STATUS_OK;
			}
		}
		break;

	default:
		Homing_Fail(HOMING_STATUS_ERROR_FAULT, ERROR_CODE_E_NONE);
		break;
	}
}

void Homing_Cancel(void)
{
	if (Homing_IsBusy() != 0U)
	{
		StepperUM244_Stop();
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_NOT_HOMED);
		s_last_status = HOMING_STATUS_CANCELLED;
		s_state = HOMING_STATE_CANCELLED;
	}
}

uint8_t Homing_IsBusy(void)
{
	return ((s_state != HOMING_STATE_IDLE) &&
	        (s_state != HOMING_STATE_COMPLETE) &&
	        (s_state != HOMING_STATE_FAULT) &&
	        (s_state != HOMING_STATE_CANCELLED)) ? 1U : 0U;
}

uint8_t Homing_IsComplete(void)
{
	return (s_state == HOMING_STATE_COMPLETE) ? 1U : 0U;
}

Homing_State_t Homing_GetState(void)
{
	return s_state;
}

Homing_Status_t Homing_GetLastStatus(void)
{
	return s_last_status;
}

uint32_t Homing_GetSearchPulses(void)
{
	return s_total_search_pulses;
}
