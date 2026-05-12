#include "error_manager.h"

static uint32_t s_error_flags;
static uint8_t s_buzzer_muted;

static uint32_t ErrorManager_Mask(ErrorCode_t code)
{
	if ((code <= ERROR_CODE_E_NONE) || (code >= ERROR_CODE_COUNT))
	{
		return 0UL;
	}

	return (uint32_t)(1UL << ((uint8_t)code - 1U));
}

void ErrorManager_Init(void)
{
	s_error_flags = 0UL;
	s_buzzer_muted = 0U;
}

void ErrorManager_Set(ErrorCode_t code)
{
	uint32_t mask;

	mask = ErrorManager_Mask(code);
	if (mask != 0UL)
	{
		s_error_flags |= mask;
	}
}

void ErrorManager_Clear(ErrorCode_t code)
{
	uint32_t mask;

	mask = ErrorManager_Mask(code);
	if (mask != 0UL)
	{
		s_error_flags &= ~mask;
	}
}

void ErrorManager_ClearAll(void)
{
	s_error_flags = 0UL;
}

uint8_t ErrorManager_IsActive(ErrorCode_t code)
{
	uint32_t mask;

	mask = ErrorManager_Mask(code);
	return ((mask != 0UL) && ((s_error_flags & mask) != 0UL)) ? 1U : 0U;
}

ErrorLevel_t ErrorManager_GetLevel(ErrorCode_t code)
{
	if ((code == ERROR_CODE_W_PARAM_DEFAULT) ||
	    (code == ERROR_CODE_W_PARAM_REJECTED))
	{
		return ERROR_LEVEL_WARNING;
	}

	if ((code > ERROR_CODE_E_NONE) && (code < ERROR_CODE_COUNT))
	{
		return ERROR_LEVEL_FAULT;
	}

	return ERROR_LEVEL_NONE;
}

uint8_t ErrorManager_HasFault(void)
{
	uint8_t i;

	for (i = 1U; i < (uint8_t)ERROR_CODE_COUNT; i++)
	{
		if ((ErrorManager_IsActive((ErrorCode_t)i) != 0U) &&
		    (ErrorManager_GetLevel((ErrorCode_t)i) == ERROR_LEVEL_FAULT))
		{
			return 1U;
		}
	}

	return 0U;
}

ErrorCode_t ErrorManager_GetPrimary(void)
{
	uint8_t i;

	for (i = 1U; i < (uint8_t)ERROR_CODE_COUNT; i++)
	{
		if ((ErrorManager_IsActive((ErrorCode_t)i) != 0U) &&
		    (ErrorManager_GetLevel((ErrorCode_t)i) == ERROR_LEVEL_FAULT))
		{
			return (ErrorCode_t)i;
		}
	}

	for (i = 1U; i < (uint8_t)ERROR_CODE_COUNT; i++)
	{
		if (ErrorManager_IsActive((ErrorCode_t)i) != 0U)
		{
			return (ErrorCode_t)i;
		}
	}

	return ERROR_CODE_E_NONE;
}

void ErrorManager_SetBuzzerMuted(uint8_t muted)
{
	s_buzzer_muted = (muted != 0U) ? 1U : 0U;
}

uint8_t ErrorManager_IsBuzzerMuted(void)
{
	return s_buzzer_muted;
}

const char *ErrorManager_GetName(ErrorCode_t code)
{
	switch (code)
	{
	case ERROR_CODE_E_NONE:
		return "E_NONE";
	case ERROR_CODE_W_PARAM_DEFAULT:
		return "W_PARAM_DEFAULT";
	case ERROR_CODE_W_PARAM_REJECTED:
		return "W_PARAM_REJECTED";
	case ERROR_CODE_E_SENSOR_AIR_FAIL:
		return "E_SENSOR_AIR_FAIL";
	case ERROR_CODE_E_SENSOR_TANK_FAIL:
		return "E_SENSOR_TANK_FAIL";
	case ERROR_CODE_E_SENSOR_BASKET_FAIL:
		return "E_SENSOR_BASKET_FAIL";
	case ERROR_CODE_E_I2C_A_FAIL:
		return "E_I2C_A_FAIL";
	case ERROR_CODE_E_I2C_B_FAIL:
		return "E_I2C_B_FAIL";
	case ERROR_CODE_E_I2C_C_FAIL:
		return "E_I2C_C_FAIL";
	case ERROR_CODE_E_TANK_LOW:
		return "E_TANK_LOW";
	case ERROR_CODE_E_TANK_HIGH:
		return "E_TANK_HIGH";
	case ERROR_CODE_E_BASKET_LOW:
		return "E_BASKET_LOW";
	case ERROR_CODE_E_BASKET_HIGH:
		return "E_BASKET_HIGH";
	case ERROR_CODE_E_WATER_JUMP:
		return "E_WATER_JUMP";
	case ERROR_CODE_E_PRESSURE_PHYSICAL:
		return "E_PRESSURE_PHYSICAL";
	case ERROR_CODE_E_UPPER_LIMIT:
		return "E_UPPER_LIMIT";
	case ERROR_CODE_E_LOWER_LIMIT:
		return "E_LOWER_LIMIT";
	case ERROR_CODE_E_LIMIT_MISMATCH:
		return "E_LIMIT_MISMATCH";
	case ERROR_CODE_E_POSITION_UNTRUSTED:
		return "E_POSITION_UNTRUSTED";
	case ERROR_CODE_E_STALL:
		return "E_STALL";
	case ERROR_CODE_E_SELF_TEST_FAIL:
		return "E_SELF_TEST_FAIL";
	case ERROR_CODE_E_MOTOR_RELEASED:
		return "E_MOTOR_RELEASED";
	default:
		return "E_UNKNOWN";
	}
}
