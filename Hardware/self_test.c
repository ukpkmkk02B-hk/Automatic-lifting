#include "self_test.h"
#include "board_config.h"
#include "buzzer.h"
#include "error_code.h"
#include "error_manager.h"
#include "limit.h"
#include "param_store.h"
#include "water_depth.h"
#include "wf5805f.h"

static uint32_t s_start_ms;
static uint32_t s_last_ms;
static uint32_t s_beep_off_ms;
static SelfTest_Status_t s_status;
static uint8_t s_checked;

// 判断启动自检等待时间是否到期，使用无符号差值兼容毫秒计数回绕。
static uint8_t SelfTest_TimeElapsed(uint32_t now_ms, uint32_t start_ms, uint32_t interval_ms)
{
	return ((uint32_t)(now_ms - start_ms) >= interval_ms) ? 1U : 0U;
}

// 检查指定 WF5805F 传感器是否已有有效读数；不主动触发 I2C 采样。
static uint8_t SelfTest_IsSensorOk(WF5805F_Sensor_t sensor)
{
	WF5805F_Reading_t reading;

	if (WF5805F_GetReading(sensor, &reading) != WF5805F_OK)
	{
		return 0U;
	}

	return (reading.valid != 0U) ? 1U : 0U;
}

// 检查 Flash 参数记录是否可用；读取失败时按默认参数验证，避免坏记录阻塞基础自检。
static uint8_t SelfTest_AreParamsOk(void)
{
	ParamStore_Record_t record;

	if (ParamStore_Load(&record) != PARAM_STORE_STATUS_OK)
	{
		ParamStore_LoadDefaults(&record);
	}

	return (ParamStore_ValidateRecord(&record) == PARAM_STORE_STATUS_OK) ? 1U : 0U;
}

// 函    数：SelfTest_RunFinalCheck
// 参    数：无
// 返 回 值：自检通过返回 1，否则返回 0。
// 注意事项：只在 10s 稳定等待结束后执行；失败时锁存明确错误，主状态机负责停机。
static uint8_t SelfTest_RunFinalCheck(void)
{
	WaterDepth_State_t depth;
	uint8_t ok;

	ok = 1U;
	if ((SelfTest_IsSensorOk(WF5805F_SENSOR_AIR) == 0U) ||
	    (SelfTest_IsSensorOk(WF5805F_SENSOR_BASKET) == 0U) ||
	    (SelfTest_IsSensorOk(WF5805F_SENSOR_TANK) == 0U))
	{
		ok = 0U;
	}

	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_LIMIT_MISMATCH);
		ok = 0U;
	}

	if (WaterDepth_GetState(&depth) != WATER_DEPTH_OK)
	{
		ok = 0U;
	}

	if (SelfTest_AreParamsOk() == 0U)
	{
		ErrorManager_Set(ERROR_CODE_W_PARAM_REJECTED);
		ok = 0U;
	}

	if ((ErrorManager_HasFault() != 0U) || (ok == 0U))
	{
		ErrorManager_Set(ERROR_CODE_E_SELF_TEST_FAIL);
		return 0U;
	}

	return 1U;
}

// 函    数：SelfTest_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：只建立自检时间戳和短鸣截止时间，不等待任何硬件。
void SelfTest_Init(uint32_t now_ms)
{
	s_start_ms = now_ms;
	s_last_ms = now_ms;
	s_beep_off_ms = now_ms + BOARD_SELF_TEST_BEEP_MS;
	s_status = SELF_TEST_STATUS_PENDING;
	s_checked = 0U;
	Buzzer_On();
}

// 函    数：SelfTest_Update
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：自检状态。
// 注意事项：传感器稳定等待用时间戳差值推进，禁止 Delay 或长 while 等待。
SelfTest_Status_t SelfTest_Update(uint32_t now_ms)
{
	s_last_ms = now_ms;
	if (s_status != SELF_TEST_STATUS_PENDING)
	{
		return s_status;
	}

	if (SelfTest_TimeElapsed(now_ms,
	                         s_start_ms,
	                         BOARD_SELF_TEST_SENSOR_STABLE_MS) == 0U)
	{
		return SELF_TEST_STATUS_PENDING;
	}

	if (s_checked == 0U)
	{
		s_checked = 1U;
		s_status = (SelfTest_RunFinalCheck() != 0U) ?
		           SELF_TEST_STATUS_PASS : SELF_TEST_STATUS_FAIL;
	}

	return s_status;
}

// 函    数：SelfTest_ServiceBuzzer
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：菜单模块也会管理报警蜂鸣；本函数只在短鸣截止后关闭一次开机短鸣。
void SelfTest_ServiceBuzzer(uint32_t now_ms)
{
	s_last_ms = now_ms;
	if (s_beep_off_ms == 0UL)
	{
		return;
	}

	if ((int32_t)(now_ms - s_beep_off_ms) >= 0)
	{
		s_beep_off_ms = 0UL;
		if ((ErrorManager_HasFault() == 0U) || (ErrorManager_IsBuzzerMuted() != 0U))
		{
			Buzzer_Off();
		}
	}
	else
	{
		Buzzer_On();
	}
}

// 函    数：SelfTest_GetContext
// 参    数：ctx 输出 OLED 自检页面上下文。
// 返 回 值：无
// 注意事项：倒计时单位为 second；I2C 状态按当前连续失败计数做启动期显示。
void SelfTest_GetContext(UiPages_SelfTestContext_t *ctx)
{
	uint32_t elapsed_ms;
	uint32_t remain_ms;

	if (ctx == 0)
	{
		return;
	}

	ctx->seconds_left_valid = 1U;
	ctx->seconds_left = 0U;
	if (s_status == SELF_TEST_STATUS_PENDING)
	{
		elapsed_ms = s_last_ms - s_start_ms;
		if (elapsed_ms < BOARD_SELF_TEST_SENSOR_STABLE_MS)
		{
			remain_ms = BOARD_SELF_TEST_SENSOR_STABLE_MS - elapsed_ms;
			ctx->seconds_left = (uint16_t)((remain_ms + 999UL) / 1000UL);
		}
	}
	ctx->i2c_a_ok = (WF5805F_GetFailureCount(WF5805F_SENSOR_AIR) == 0U) ? 1U : 0U;
	ctx->i2c_b_ok = (WF5805F_GetFailureCount(WF5805F_SENSOR_BASKET) == 0U) ? 1U : 0U;
	ctx->i2c_c_ok = (WF5805F_GetFailureCount(WF5805F_SENSOR_TANK) == 0U) ? 1U : 0U;
	Limit_GetState(&ctx->limits);
}

// 函    数：SelfTest_GetStatus
// 参    数：无
// 返 回 值：最近一次自检状态。
SelfTest_Status_t SelfTest_GetStatus(void)
{
	return s_status;
}
