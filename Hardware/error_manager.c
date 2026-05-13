#include "error_manager.h"

static uint32_t s_error_flags;
static uint8_t s_buzzer_muted;

// 函    数：ErrorManager_Mask
// 参    数：code 错误码。
// 返 回 值：错误码对应的锁存位掩码；E_NONE 或非法值返回 0。
// 注意事项：bit0 对应第一个真实错误码，当前最多支持 32 个错误锁存位。
static uint32_t ErrorManager_Mask(ErrorCode_t code)
{
	if ((code <= ERROR_CODE_E_NONE) || (code >= ERROR_CODE_COUNT))
	{
		// E_NONE 和非法值没有锁存位，避免误清或误置。
		return 0UL;
	}

	// 使用 bit0 对应第一个真实错误码，最多支持 32 个错误锁存位。
	return (uint32_t)(1UL << ((uint8_t)code - 1U));
}

// 函    数：ErrorManager_Init
// 参    数：无
// 返 回 值：无
// 注意事项：系统初始化时清空错误锁存和蜂鸣器静音标志。
void ErrorManager_Init(void)
{
	s_error_flags = 0UL;
	s_buzzer_muted = 0U;
}

// 函    数：ErrorManager_Set
// 参    数：code 需要锁存的错误码。
// 返 回 值：无
// 注意事项：错误置位采用锁存方式，直到受控恢复流程显式清除。
void ErrorManager_Set(ErrorCode_t code)
{
	uint32_t mask;

	mask = ErrorManager_Mask(code);
	if (mask != 0UL)
	{
		// 错误置位采用锁存方式，直到受控恢复流程显式清除。
		s_error_flags |= mask;
	}
}

// 函    数：ErrorManager_Clear
// 参    数：code 需要清除的错误码。
// 返 回 值：无
// 注意事项：只清除指定错误位；严重故障应在原因消除并人工确认后才清除。
void ErrorManager_Clear(ErrorCode_t code)
{
	uint32_t mask;

	mask = ErrorManager_Mask(code);
	if (mask != 0UL)
	{
		s_error_flags &= ~mask;
	}
}

// 函    数：ErrorManager_ClearAll
// 参    数：无
// 返 回 值：无
// 注意事项：清除错误不影响蜂鸣器静音标志，静音状态由独立接口管理。
void ErrorManager_ClearAll(void)
{
	// 清除错误不影响蜂鸣器静音标志，静音状态由独立接口管理。
	s_error_flags = 0UL;
}

// 函    数：ErrorManager_IsActive
// 参    数：code 需要查询的错误码。
// 返 回 值：1 表示该错误处于锁存状态，0 表示未锁存或参数非法。
// 注意事项：该函数不判断错误级别，只检查锁存位。
uint8_t ErrorManager_IsActive(ErrorCode_t code)
{
	uint32_t mask;

	mask = ErrorManager_Mask(code);
	return ((mask != 0UL) && ((s_error_flags & mask) != 0UL)) ? 1U : 0U;
}

// 函    数：ErrorManager_GetLevel
// 参    数：code 需要判断级别的错误码。
// 返 回 值：警告、严重故障或无错误。
// 注意事项：参数相关问题先作为警告，防止默认值恢复被误当作硬故障。
ErrorLevel_t ErrorManager_GetLevel(ErrorCode_t code)
{
	if ((code == ERROR_CODE_W_PARAM_DEFAULT) ||
	    (code == ERROR_CODE_W_PARAM_REJECTED))
	{
		// 参数相关问题先作为警告，防止误把默认值恢复当作硬故障。
		return ERROR_LEVEL_WARNING;
	}

	if ((code > ERROR_CODE_E_NONE) && (code < ERROR_CODE_COUNT))
	{
		return ERROR_LEVEL_FAULT;
	}

	return ERROR_LEVEL_NONE;
}

// 函    数：ErrorManager_HasFault
// 参    数：无
// 返 回 值：存在任一严重故障返回 1，否则返回 0。
// 注意事项：任一严重故障存在时，上层状态机应停止自动运动。
uint8_t ErrorManager_HasFault(void)
{
	uint8_t i;

	for (i = 1U; i < (uint8_t)ERROR_CODE_COUNT; i++)
	{
		// 任一严重故障存在时，上层状态机应停止自动运动。
		if ((ErrorManager_IsActive((ErrorCode_t)i) != 0U) &&
		    (ErrorManager_GetLevel((ErrorCode_t)i) == ERROR_LEVEL_FAULT))
		{
			return 1U;
		}
	}

	return 0U;
}

// 函    数：ErrorManager_GetPrimary
// 参    数：无
// 返 回 值：当前最高优先级错误码；无错误时返回 ERROR_CODE_E_NONE。
// 注意事项：OLED/报警优先显示严重故障，避免警告遮盖停机原因。
ErrorCode_t ErrorManager_GetPrimary(void)
{
	uint8_t i;

	for (i = 1U; i < (uint8_t)ERROR_CODE_COUNT; i++)
	{
		// OLED/报警优先显示严重故障，避免警告遮盖停机原因。
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

// 函    数：ErrorManager_SetBuzzerMuted
// 参    数：muted 非 0 表示静音，0 表示取消静音。
// 返 回 值：无
// 注意事项：蜂鸣器静音只关闭声音提示，不清除错误锁存。
void ErrorManager_SetBuzzerMuted(uint8_t muted)
{
	// 蜂鸣器静音只关闭声音提示，不清除错误锁存。
	s_buzzer_muted = (muted != 0U) ? 1U : 0U;
}

// 函    数：ErrorManager_IsBuzzerMuted
// 参    数：无
// 返 回 值：1 表示蜂鸣器静音标志已置位，0 表示未静音。
// 注意事项：静音状态和错误锁存相互独立。
uint8_t ErrorManager_IsBuzzerMuted(void)
{
	return s_buzzer_muted;
}

// 函    数：ErrorManager_GetName
// 参    数：code 需要转换为 ASCII 名称的错误码。
// 返 回 值：错误码名称字符串，未知错误返回 E_UNKNOWN。
// 注意事项：供 OLED 或调试输出使用，字符串为静态常量，不需要释放。
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
