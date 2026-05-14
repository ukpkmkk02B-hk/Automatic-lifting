#include "ui_pages.h"
#include "OLED.h"

// 函    数：UiPages_ClearLine
// 参    数：line 目标 16 字符行缓冲。
// 返 回 值：无
// 注意事项：OLED 每行固定 16 个 ASCII 字符；尾部补空格用于清除上一次显示残留。
static void UiPages_ClearLine(char *line)
{
	uint8_t i;

	for (i = 0U; i < UI_PAGES_LINE_CHARS; i++)
	{
		line[i] = ' ';
	}
	line[UI_PAGES_LINE_CHARS] = '\0';
}

static void UiPages_ClearFrame(UiPages_Frame_t frame)
{
	uint8_t row;

	for (row = 0U; row < UI_PAGES_ROW_COUNT; row++)
	{
		UiPages_ClearLine(frame[row]);
	}
}

static void UiPages_WriteText(char *line, uint8_t column, const char *text)
{
	uint8_t i;

	if ((line == 0) || (text == 0) || (column >= UI_PAGES_LINE_CHARS))
	{
		return;
	}

	i = 0U;
	while ((text[i] != '\0') && ((uint8_t)(column + i) < UI_PAGES_LINE_CHARS))
	{
		line[column + i] = text[i];
		i++;
	}
}

static void UiPages_WriteDigit(char *line, uint8_t column, uint8_t digit)
{
	if (column < UI_PAGES_LINE_CHARS)
	{
		line[column] = (char)('0' + (digit % 10U));
	}
}

static void UiPages_WriteUint(char *line, uint8_t column, uint8_t width, uint32_t value)
{
	uint8_t i;
	uint8_t pos;

	for (i = 0U; i < width; i++)
	{
		pos = (uint8_t)(column + width - 1U - i);
		if (pos < UI_PAGES_LINE_CHARS)
		{
			UiPages_WriteDigit(line, pos, (uint8_t)(value % 10UL));
		}
		value /= 10UL;
	}
}

static uint32_t UiPages_Abs32(int32_t value)
{
	return (value < 0L) ? (uint32_t)(-value) : (uint32_t)value;
}

static uint8_t UiPages_TextLenLimited(const char *text, uint8_t max_len)
{
	uint8_t len;

	len = 0U;
	if (text == 0)
	{
		return 0U;
	}

	while ((text[len] != '\0') && (len < max_len))
	{
		len++;
	}

	return len;
}

// 函    数：UiPages_WriteDepth3x1
// 参    数：line 行缓冲；column 起始列；value_mm_x10 水深，单位 mm_x10；valid 非 0 表示有效。
// 返 回 值：无
// 注意事项：输出宽度固定 5 字符，例如 100.0；无效值输出 --.- 并保留 1 个空格清尾。
static void UiPages_WriteDepth3x1(char *line, uint8_t column, int32_t value_mm_x10, uint8_t valid)
{
	uint32_t abs_value;

	if (valid == 0U)
	{
		UiPages_WriteText(line, column, "--.- ");
		return;
	}

	abs_value = UiPages_Abs32(value_mm_x10);
	if (abs_value > 9999UL)
	{
		abs_value = 9999UL;
	}

	if (value_mm_x10 < 0L)
	{
		line[column] = '-';
		UiPages_WriteUint(line, (uint8_t)(column + 1U), 2U, abs_value / 10UL);
	}
	else
	{
		UiPages_WriteUint(line, column, 3U, abs_value / 10UL);
	}
	line[column + 3U] = '.';
	UiPages_WriteDigit(line, (uint8_t)(column + 4U), (uint8_t)(abs_value % 10UL));
}

static void UiPages_WriteDepth4x1(char *line, uint8_t column, int32_t value_mm_x10, uint8_t valid)
{
	uint32_t abs_value;

	if (valid == 0U)
	{
		UiPages_WriteText(line, column, "--.-  ");
		return;
	}

	abs_value = UiPages_Abs32(value_mm_x10);
	if (abs_value > 99999UL)
	{
		abs_value = 99999UL;
	}

	if (value_mm_x10 < 0L)
	{
		line[column] = '-';
		UiPages_WriteUint(line, (uint8_t)(column + 1U), 3U, abs_value / 10UL);
	}
	else
	{
		UiPages_WriteUint(line, column, 4U, abs_value / 10UL);
	}
	line[column + 4U] = '.';
	UiPages_WriteDigit(line, (uint8_t)(column + 5U), (uint8_t)(abs_value % 10UL));
}

static void UiPages_WritePressure4x2(char *line, uint8_t column, int32_t value_hpa_x100, uint8_t valid)
{
	uint32_t abs_value;

	if (valid == 0U)
	{
		UiPages_WriteText(line, column, "----.--");
		return;
	}

	abs_value = UiPages_Abs32(value_hpa_x100);
	if (abs_value > 999999UL)
	{
		abs_value = 999999UL;
	}

	if (value_hpa_x100 < 0L)
	{
		line[column] = '-';
		UiPages_WriteUint(line, (uint8_t)(column + 1U), 3U, abs_value / 100UL);
	}
	else
	{
		UiPages_WriteUint(line, column, 4U, abs_value / 100UL);
	}
	line[column + 4U] = '.';
	UiPages_WriteUint(line, (uint8_t)(column + 5U), 2U, abs_value % 100UL);
}

static void UiPages_WriteTime(char *line, uint8_t column, uint32_t seconds, uint8_t valid)
{
	uint32_t minutes;
	uint32_t hours;

	if (valid == 0U)
	{
		UiPages_WriteText(line, column, "--m--s");
		return;
	}

	if (seconds < 3600UL)
	{
		minutes = seconds / 60UL;
		if (minutes > 999UL)
		{
			minutes = 999UL;
		}
		UiPages_WriteUint(line, column, 3U, minutes);
		line[column + 3U] = 'm';
		UiPages_WriteUint(line, (uint8_t)(column + 4U), 2U, seconds % 60UL);
		line[column + 6U] = 's';
	}
	else
	{
		hours = seconds / 3600UL;
		minutes = (seconds % 3600UL) / 60UL;
		if (hours > 99UL)
		{
			hours = 99UL;
		}
		UiPages_WriteUint(line, column, 2U, hours);
		line[column + 2U] = 'h';
		UiPages_WriteUint(line, (uint8_t)(column + 3U), 2U, minutes);
		line[column + 5U] = 'm';
	}
}

static const char *UiPages_ModeText(UiPages_Mode_t mode)
{
	switch (mode)
	{
	case UI_PAGES_MODE_AUTO:
		return "AUTO";
	case UI_PAGES_MODE_PAUSED:
		return "PAUS";
	case UI_PAGES_MODE_MANUAL:
		return "MANU";
	case UI_PAGES_MODE_MAINTENANCE:
		return "MAINT";
	case UI_PAGES_MODE_FAULT:
		return "FAULT";
	case UI_PAGES_MODE_SELF_TEST:
	default:
		return "SELF";
	}
}

static const char *UiPages_ErrorAlias(ErrorCode_t code)
{
	switch (code)
	{
	case ERROR_CODE_E_NONE:
		return "NONE";
	case ERROR_CODE_W_PARAM_DEFAULT:
		return "PARAM DEF";
	case ERROR_CODE_W_PARAM_REJECTED:
		return "PARAM ERR";
	case ERROR_CODE_E_SENSOR_AIR_FAIL:
		return "AIR FAIL";
	case ERROR_CODE_E_SENSOR_TANK_FAIL:
		return "TNK FAIL";
	case ERROR_CODE_E_SENSOR_BASKET_FAIL:
		return "BAS FAIL";
	case ERROR_CODE_E_I2C_A_FAIL:
		return "I2C-A FAIL";
	case ERROR_CODE_E_I2C_B_FAIL:
		return "I2C-B FAIL";
	case ERROR_CODE_E_I2C_C_FAIL:
		return "I2C-C FAIL";
	case ERROR_CODE_E_TANK_LOW:
		return "TANK LOW";
	case ERROR_CODE_E_TANK_HIGH:
		return "TANK HIGH";
	case ERROR_CODE_E_BASKET_LOW:
		return "BASKET LOW";
	case ERROR_CODE_E_BASKET_HIGH:
		return "BASKET HIGH";
	case ERROR_CODE_E_WATER_JUMP:
		return "WATER JUMP";
	case ERROR_CODE_E_PRESSURE_PHYSICAL:
		return "PRESS PHYS";
	case ERROR_CODE_E_UPPER_LIMIT:
		return "UP LIMIT";
	case ERROR_CODE_E_LOWER_LIMIT:
		return "DN LIMIT";
	case ERROR_CODE_E_LIMIT_MISMATCH:
		return "LIM MISMT";
	case ERROR_CODE_E_POSITION_UNTRUSTED:
		return "POS UNTR";
	case ERROR_CODE_E_STALL:
		return "STALL";
	case ERROR_CODE_E_SELF_TEST_FAIL:
		return "SELF FAIL";
	case ERROR_CODE_E_MOTOR_RELEASED:
		return "MOTOR REL";
	default:
		return "UNKNOWN";
	}
}

static const char *UiPages_ErrorCodeLabel(ErrorCode_t code)
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
		return "E_SENSOR_AIR";
	case ERROR_CODE_E_SENSOR_TANK_FAIL:
		return "E_SENSOR_TANK";
	case ERROR_CODE_E_SENSOR_BASKET_FAIL:
		return "E_SENSOR_BAS";
	case ERROR_CODE_E_I2C_A_FAIL:
		return "E_I2C_A";
	case ERROR_CODE_E_I2C_B_FAIL:
		return "E_I2C_B";
	case ERROR_CODE_E_I2C_C_FAIL:
		return "E_I2C_C";
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
		return "E_PRESS_PHYS";
	case ERROR_CODE_E_UPPER_LIMIT:
		return "E_UPPER_LIMIT";
	case ERROR_CODE_E_LOWER_LIMIT:
		return "E_LOWER_LIMIT";
	case ERROR_CODE_E_LIMIT_MISMATCH:
		return "E_LIMIT_MISMT";
	case ERROR_CODE_E_POSITION_UNTRUSTED:
		return "E_POS_UNTRUST";
	case ERROR_CODE_E_STALL:
		return "E_STALL";
	case ERROR_CODE_E_SELF_TEST_FAIL:
		return "E_SELF_TEST";
	case ERROR_CODE_E_MOTOR_RELEASED:
		return "E_MOTOR_REL";
	default:
		return "E_UNKNOWN";
	}
}

void UiPages_RenderFrame(UiPages_Frame_t frame)
{
	uint8_t row;

	for (row = 0U; row < UI_PAGES_ROW_COUNT; row++)
	{
		OLED_ShowString((uint8_t)(row + 1U), 1U, (char *)frame[row]);
	}
}

void UiPages_FormatMain(const UiPages_MainContext_t *ctx, UiPages_Frame_t frame)
{
	const char *motion;
	const char *mode_text;
	uint8_t mode_len;
	uint8_t motion_col;
	uint32_t run_days;
	uint32_t today_pulses;

	UiPages_ClearFrame(frame);
	if (ctx == 0)
	{
		UiPages_WriteText(frame[0], 0U, "UI DATA INVALID");
		return;
	}

	UiPages_WriteText(frame[0], 0U, "B:");
	UiPages_WriteDepth3x1(frame[0], 2U, ctx->basket_depth_mm_x10, ctx->basket_depth_valid);
	UiPages_WriteText(frame[0], 8U, "T:");
	UiPages_WriteDepth3x1(frame[0], 10U, ctx->target_depth_mm_x10, ctx->target_depth_valid);

	run_days = ctx->run_days;
	if (run_days > 999UL)
	{
		run_days = 999UL;
	}
	frame[1][0] = 'D';
	UiPages_WriteUint(frame[1], 1U, 3U, run_days);
	mode_text = UiPages_ModeText(ctx->mode);
	UiPages_WriteText(frame[1], 5U, mode_text);
	mode_len = UiPages_TextLenLimited(mode_text, 5U);
	motion_col = (uint8_t)(5U + mode_len + 1U);
	motion = (ctx->motion_text != 0) ? ctx->motion_text : "---";
	UiPages_WriteText(frame[1], motion_col, motion);

	UiPages_WriteText(frame[2], 0U, "NXT ");
	UiPages_WriteTime(frame[2], 4U, ctx->next_nap_remaining_s, ctx->next_nap_valid);

	today_pulses = ctx->today_done_pulses;
	if (today_pulses > 999UL)
	{
		today_pulses = 999UL;
	}
	frame[3][0] = 'P';
	UiPages_WriteUint(frame[3], 1U, 3U, today_pulses);
	UiPages_WriteText(frame[3], 5U, "ERR");
	if (ctx->primary_error == ERROR_CODE_E_NONE)
	{
		UiPages_WriteText(frame[3], 8U, "--");
	}
	else
	{
		frame[3][8] = '!';
	}
	UiPages_WriteText(frame[3], 11U, (ctx->buzzer_muted != 0U) ? "M-Y" : "M-N");
}

void UiPages_FormatSelfTest(const UiPages_SelfTestContext_t *ctx, UiPages_Frame_t frame)
{
	Limit_State_t limits;

	UiPages_ClearFrame(frame);
	UiPages_WriteText(frame[0], 0U, "SELF TEST");
	UiPages_WriteText(frame[1], 0U, "WAIT SENSOR ");
	if ((ctx != 0) && (ctx->seconds_left_valid != 0U))
	{
		UiPages_WriteUint(frame[1], 12U, 2U, ctx->seconds_left);
		frame[1][14] = 's';
	}
	else
	{
		UiPages_WriteText(frame[1], 12U, "--s");
	}
	UiPages_WriteText(frame[2], 0U, "I2C A- B- C-");
	if (ctx != 0)
	{
		frame[2][5] = (ctx->i2c_a_ok != 0U) ? '0' : 'X';
		frame[2][8] = (ctx->i2c_b_ok != 0U) ? '0' : 'X';
		frame[2][11] = (ctx->i2c_c_ok != 0U) ? '0' : 'X';
		limits = ctx->limits;
	}
	else
	{
		limits.left_upper = 0U;
		limits.left_lower = 0U;
		limits.right_upper = 0U;
		limits.right_lower = 0U;
	}
	UiPages_WriteText(frame[3], 0U, "LIM ");
	frame[3][4] = (limits.left_upper != 0U) ? '1' : '0';
	frame[3][5] = (limits.left_lower != 0U) ? '1' : '0';
	frame[3][6] = (limits.right_upper != 0U) ? '1' : '0';
	frame[3][7] = (limits.right_lower != 0U) ? '1' : '0';
}

void UiPages_FormatSensor(const UiPages_SensorContext_t *ctx, UiPages_Frame_t frame)
{
	UiPages_ClearFrame(frame);
	UiPages_WriteText(frame[0], 0U, "AIR ");
	UiPages_WriteText(frame[1], 0U, "BAS ");
	UiPages_WriteText(frame[2], 0U, "TNK ");
	UiPages_WriteText(frame[3], 0U, "I2C A0 B0 C0");

	if (ctx == 0)
	{
		UiPages_WriteText(frame[0], 4U, "----.--hPa");
		UiPages_WriteText(frame[1], 4U, "--.-mm");
		UiPages_WriteText(frame[2], 4U, "--.-mm");
		return;
	}

	UiPages_WritePressure4x2(frame[0], 4U, ctx->air_pressure_hpa_x100, ctx->air_pressure_valid);
	UiPages_WriteText(frame[0], 11U, "hPa");
	UiPages_WriteDepth4x1(frame[1], 4U, ctx->basket_depth_mm_x10, ctx->basket_depth_valid);
	UiPages_WriteText(frame[1], 10U, "mm");
	UiPages_WriteDepth4x1(frame[2], 4U, ctx->tank_depth_mm_x10, ctx->tank_depth_valid);
	UiPages_WriteText(frame[2], 10U, "mm");
	UiPages_WriteUint(frame[3], 5U, 1U, ctx->i2c_a_failures);
	UiPages_WriteUint(frame[3], 8U, 1U, ctx->i2c_b_failures);
	UiPages_WriteUint(frame[3], 11U, 1U, ctx->i2c_c_failures);
}

void UiPages_FormatLimit(const UiPages_LimitContext_t *ctx, UiPages_Frame_t frame)
{
	Limit_State_t limits;

	UiPages_ClearFrame(frame);
	if (ctx != 0)
	{
		limits = ctx->limits;
	}
	else
	{
		limits.left_upper = 0U;
		limits.left_lower = 0U;
		limits.right_upper = 0U;
		limits.right_lower = 0U;
	}

	UiPages_WriteText(frame[0], 0U, "LU0 LD0 RU0 RD0");
	frame[0][2] = (limits.left_upper != 0U) ? '1' : '0';
	frame[0][6] = (limits.left_lower != 0U) ? '1' : '0';
	frame[0][10] = (limits.right_upper != 0U) ? '1' : '0';
	frame[0][14] = (limits.right_lower != 0U) ? '1' : '0';
	UiPages_WriteText(frame[1], 0U, ((ctx != 0) && (ctx->upper_blocked != 0U)) ? "UP BLK " : "UP OK  ");
	UiPages_WriteText(frame[1], 8U, ((ctx != 0) && (ctx->lower_blocked != 0U)) ? "DN BLK" : "DN OK");
	UiPages_WriteText(frame[2], 0U, "POS ");
	if (ctx != 0)
	{
		UiPages_WriteDepth3x1(frame[2], 4U, ctx->position_mm_x10, ctx->position_valid);
		UiPages_WriteText(frame[2], 9U, "mm ");
		UiPages_WriteText(frame[2], 12U, (ctx->position_trusted != 0U) ? "OK" : "UN");
	}
	else
	{
		UiPages_WriteText(frame[2], 4U, "--.-mm UN");
	}
	UiPages_WriteText(frame[3], 0U, "LIM ERR:");
	if ((ctx != 0) && (ctx->limit_mismatch != 0U))
	{
		UiPages_WriteText(frame[3], 8U, "MISMT");
	}
	else if ((ctx != 0) && (ctx->upper_blocked != 0U))
	{
		UiPages_WriteText(frame[3], 8U, "UPPER");
	}
	else if ((ctx != 0) && (ctx->lower_blocked != 0U))
	{
		UiPages_WriteText(frame[3], 8U, "LOWER");
	}
	else
	{
		UiPages_WriteText(frame[3], 8U, "NONE");
	}
}

void UiPages_FormatParam(const UiPages_ParamContext_t *ctx, UiPages_Frame_t frame)
{
	const ParamStore_Record_t *record;

	UiPages_ClearFrame(frame);
	if ((ctx == 0) || (ctx->record == 0))
	{
		UiPages_WriteText(frame[0], 0U, "PARAM ERROR");
		UiPages_WriteText(frame[1], 0U, "NO RECORD");
		UiPages_WriteText(frame[2], 0U, "KEEP OLD VALUE");
		UiPages_WriteText(frame[3], 0U, "PB10 CONFIRM");
		return;
	}

	record = ctx->record;
	if (ctx->save_error != 0U)
	{
		UiPages_WriteText(frame[0], 0U, "PARAM ERROR");
		UiPages_WriteText(frame[1], 0U, "OUT OF RANGE");
		UiPages_WriteText(frame[2], 0U, "KEEP OLD VALUE");
		UiPages_WriteText(frame[3], 0U, "PB10 CONFIRM");
		return;
	}

	switch (ctx->param_id)
	{
	case UI_PAGES_PARAM_INITIAL_DEPTH:
		UiPages_WriteText(frame[0], 0U, "P1 INIT DEPTH");
		UiPages_WriteText(frame[1], 0U, "VAL ");
		UiPages_WriteDepth3x1(frame[1], 4U, record->initial_target_mm_x10, 1U);
		UiPages_WriteText(frame[1], 9U, "mm");
		UiPages_WriteText(frame[2], 0U, "RNG 008-100mm");
		break;
	case UI_PAGES_PARAM_FINAL_DEPTH:
		UiPages_WriteText(frame[0], 0U, "P2 FINAL DEPTH");
		UiPages_WriteText(frame[1], 0U, "VAL ");
		UiPages_WriteDepth3x1(frame[1], 4U, record->final_target_mm_x10, 1U);
		UiPages_WriteText(frame[1], 9U, "mm");
		UiPages_WriteText(frame[2], 0U, "RNG 008-100mm");
		break;
	case UI_PAGES_PARAM_DAILY_RATE:
		UiPages_WriteText(frame[0], 0U, "P3 DAILY RATE");
		UiPages_WriteText(frame[1], 0U, "VAL ");
		UiPages_WriteDepth3x1(frame[1], 4U, record->daily_shallow_mm_x10, 1U);
		UiPages_WriteText(frame[1], 9U, "mm/d");
		UiPages_WriteText(frame[2], 0U, "RNG 0.0-2.0");
		break;
	case UI_PAGES_PARAM_NAP_PULSE:
		UiPages_WriteText(frame[0], 0U, "P4 NAP PULSE");
		UiPages_WriteText(frame[1], 0U, "VAL ");
		UiPages_WriteUint(frame[1], 4U, 3U, record->nap_pulses);
		UiPages_WriteText(frame[1], 7U, "pls");
		UiPages_WriteText(frame[2], 0U, "RNG 001-016");
		break;
	case UI_PAGES_PARAM_MANUAL_SPEED:
	default:
		UiPages_WriteText(frame[0], 0U, "P5 MAN SPEED");
		UiPages_WriteText(frame[1], 0U, "VAL 1.0mm/s");
		UiPages_WriteText(frame[2], 0U, "READ ONLY");
		break;
	}

	if (ctx->dirty != 0U)
	{
		UiPages_WriteText(frame[3], 0U, "PB1- PB11+ OK*");
	}
	else
	{
		UiPages_WriteText(frame[3], 0U, "PB1- PB11+ OK");
	}
}

void UiPages_FormatManual(const UiPages_ManualContext_t *ctx, UiPages_Frame_t frame)
{
	UiPages_ClearFrame(frame);
	UiPages_WriteText(frame[0], 0U, "MANUAL ");
	if (ctx == 0)
	{
		UiPages_WriteText(frame[0], 7U, "STOP");
		UiPages_WriteText(frame[1], 0U, "DEP B:--.-mm");
		UiPages_WriteText(frame[2], 0U, "POS --.-mm");
		UiPages_WriteText(frame[3], 0U, "PB1 DN PB11 UP");
		return;
	}

	if (ctx->state == UI_PAGES_MANUAL_UP)
	{
		UiPages_WriteText(frame[0], 7U, "UP");
	}
	else if (ctx->state == UI_PAGES_MANUAL_DOWN)
	{
		UiPages_WriteText(frame[0], 7U, "DOWN");
	}
	else if ((ctx->state == UI_PAGES_MANUAL_BLOCKED) || (ctx->limit_blocked != 0U))
	{
		UiPages_WriteText(frame[0], 7U, "LIMIT");
	}
	else
	{
		UiPages_WriteText(frame[0], 7U, "STOP");
	}

	UiPages_WriteText(frame[1], 0U, "DEP B:");
	UiPages_WriteDepth3x1(frame[1], 6U, ctx->basket_depth_mm_x10, ctx->basket_depth_valid);
	UiPages_WriteText(frame[1], 11U, "mm");
	UiPages_WriteText(frame[2], 0U, "POS ");
	UiPages_WriteDepth3x1(frame[2], 4U, ctx->position_mm_x10, ctx->position_valid);
	UiPages_WriteText(frame[2], 9U, "mm");
	UiPages_WriteText(frame[3], 0U, (ctx->limit_blocked != 0U) ? "LIMIT BLOCKED" : "PB1 DN PB11 UP");
}

void UiPages_FormatAlarm(const UiPages_AlarmContext_t *ctx, UiPages_Frame_t frame)
{
	ErrorCode_t primary;
	uint8_t count;
	uint8_t muted;

	UiPages_ClearFrame(frame);
	primary = (ctx != 0) ? ctx->primary_error : ERROR_CODE_E_NONE;
	count = (ctx != 0) ? ctx->active_error_count : 0U;
	muted = (ctx != 0) ? ctx->buzzer_muted : 0U;

	if (primary == ERROR_CODE_E_NONE)
	{
		UiPages_WriteText(frame[0], 0U, "ALARM NONE");
		UiPages_WriteText(frame[1], 0U, "NO ERROR");
		UiPages_WriteText(frame[2], 0U, "+0 MORE MUTE:");
		frame[2][13] = (muted != 0U) ? 'Y' : 'N';
		UiPages_WriteText(frame[3], 0U, "PB10 CLEAR");
		return;
	}

	UiPages_WriteText(frame[0], 0U, "FAULT ");
	UiPages_WriteText(frame[0], 6U, UiPages_ErrorAlias(primary));
	UiPages_WriteText(frame[1], 0U, UiPages_ErrorCodeLabel(primary));
	frame[2][0] = '+';
	UiPages_WriteUint(frame[2], 1U, 1U, (count > 0U) ? (uint32_t)(count - 1U) : 0UL);
	UiPages_WriteText(frame[2], 3U, "MORE MUTE:");
	frame[2][13] = (muted != 0U) ? 'Y' : 'N';
	UiPages_WriteText(frame[3], 0U, (muted != 0U) ? "PB10 CLEAR" : "PB10 MUTE");
}

void UiPages_FormatMaintenance(const UiPages_MaintContext_t *ctx, UiPages_Frame_t frame)
{
	uint8_t index;
	uint8_t count;

	UiPages_ClearFrame(frame);
	if (ctx == 0)
	{
		UiPages_WriteText(frame[0], 0U, "MAINT MENU 1/4");
		UiPages_WriteText(frame[1], 0U, "1 CAL AIR");
		UiPages_WriteText(frame[2], 0U, "2 HOME ZERO");
		UiPages_WriteText(frame[3], 0U, "PB10 OK PB0 BK");
		return;
	}

	if (ctx->view == UI_PAGES_MAINT_CONFIRM)
	{
		UiPages_WriteText(frame[0], 0U, "CONFIRM ");
		UiPages_WriteText(frame[0], 8U, (ctx->line2 != 0) ? ctx->line2 : "ACTION");
		UiPages_WriteText(frame[1], 0U, (ctx->line3 != 0) ? ctx->line3 : "MOTOR MAY MOVE");
		UiPages_WriteText(frame[2], 0U, "PB10 YES");
		UiPages_WriteText(frame[3], 0U, "PB0 NO");
		return;
	}

	if (ctx->view == UI_PAGES_MAINT_DEBUG)
	{
		UiPages_WriteText(frame[0], 0U, "MAINT DEBUG");
		UiPages_WriteText(frame[1], 0U, (ctx->motor_released != 0U) ? "MF RELEASED" : "MF HOLD");
		UiPages_WriteText(frame[2], 0U, (ctx->position_trusted != 0U) ? "POS TRUSTED" : "POS UNTRUST");
		UiPages_WriteText(frame[3], 0U, (ctx->homing_busy != 0U) ? "HOME BUSY" : "HOME IDLE");
		return;
	}

	index = ctx->menu_index;
	count = (ctx->menu_count == 0U) ? 4U : ctx->menu_count;
	UiPages_WriteText(frame[0], 0U, "MAINT MENU ");
	UiPages_WriteUint(frame[0], 11U, 1U, (uint32_t)(index + 1U));
	frame[0][12] = '/';
	UiPages_WriteUint(frame[0], 13U, 1U, count);

	switch (index)
	{
	case 0U:
		UiPages_WriteText(frame[1], 0U, "1 CAL AIR");
		UiPages_WriteText(frame[2], 0U, "SAVE OFFSET");
		break;
	case 1U:
		UiPages_WriteText(frame[1], 0U, "2 HOME ZERO");
		UiPages_WriteText(frame[2], 0U, "MOTOR WILL MOVE");
		break;
	case 2U:
		UiPages_WriteText(frame[1], 0U, "3 MOTOR HOLD");
		UiPages_WriteText(frame[2], 0U, (ctx->motor_released != 0U) ? "PB10 TO HOLD" : "PB10 RELEASE");
		break;
	default:
		UiPages_WriteText(frame[1], 0U, "4 DEBUG READ");
		UiPages_WriteText(frame[2], 0U, (ctx->position_trusted != 0U) ? "POS OK" : "POS UNTRUST");
		break;
	}
	UiPages_WriteText(frame[3], 0U, "PB10 OK PB0 BK");
}

void UiPages_RenderMain(const UiPages_MainContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatMain(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderSelfTest(const UiPages_SelfTestContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatSelfTest(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderSensor(const UiPages_SensorContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatSensor(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderLimit(const UiPages_LimitContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatLimit(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderParam(const UiPages_ParamContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatParam(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderManual(const UiPages_ManualContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatManual(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderAlarm(const UiPages_AlarmContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatAlarm(ctx, frame);
	UiPages_RenderFrame(frame);
}

void UiPages_RenderMaintenance(const UiPages_MaintContext_t *ctx)
{
	UiPages_Frame_t frame;

	UiPages_FormatMaintenance(ctx, frame);
	UiPages_RenderFrame(frame);
}
