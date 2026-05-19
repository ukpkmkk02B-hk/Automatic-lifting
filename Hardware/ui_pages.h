#ifndef __UI_PAGES_H
#define __UI_PAGES_H

#include "stm32f10x.h"
#include "error_code.h"
#include "limit.h"
#include "param_store.h"
#include "wf5805f.h"

// 模    块：OLED 页面渲染
// 职    责：把菜单/状态机提供的数据格式化为 4 行 x 16 字符 ASCII 页面，并输出到现有 OLED 驱动。
// 安全假设：本模块只负责显示，不修改参数、不写 Flash、不启动电机；安全动作必须由 menu/app_state 等上层模块执行。
#define UI_PAGES_ROW_COUNT               4U
#define UI_PAGES_LINE_CHARS              16U

typedef char UiPages_Frame_t[UI_PAGES_ROW_COUNT][UI_PAGES_LINE_CHARS + 1U];

typedef enum
{
	// 开机自检页面，阶段 7 仅显示占位状态，完整自检由阶段 8 接管。
	UI_PAGES_MODE_SELF_TEST = 0,
	// 自动运行或等待打盹。
	UI_PAGES_MODE_AUTO,
	// 暂停等待人工确认。
	UI_PAGES_MODE_PAUSED,
	// 手动点动页面。
	UI_PAGES_MODE_MANUAL,
	// 维护模式。
	UI_PAGES_MODE_MAINTENANCE,
	// 严重故障页面。
	UI_PAGES_MODE_FAULT
} UiPages_Mode_t;

typedef enum
{
	// P1：初始目标水深，单位 mm_x10。
	UI_PAGES_PARAM_INITIAL_DEPTH = 0,
	// P2：最终目标水深，单位 mm_x10。
	UI_PAGES_PARAM_FINAL_DEPTH,
	// P3：每日变浅量，单位 mm_x10/day。
	UI_PAGES_PARAM_DAILY_RATE,
	// P4：单次打盹脉冲数，单位 pulse。
	UI_PAGES_PARAM_NAP_PULSE,
	// P5：手动速度，只读，固定 1.0mm/s。
	UI_PAGES_PARAM_MANUAL_SPEED,
	UI_PAGES_PARAM_COUNT
} UiPages_ParamId_t;

typedef enum
{
	// 正常停止或无手动方向。
	UI_PAGES_MANUAL_STOP = 0,
	// 正在向上点动，框篮水深变浅。
	UI_PAGES_MANUAL_UP,
	// 正在向下点动，框篮水深变深。
	UI_PAGES_MANUAL_DOWN,
	// 当前方向被限位或左右限位不一致禁止。
	UI_PAGES_MANUAL_BLOCKED
} UiPages_ManualState_t;

typedef enum
{
	// 维护菜单列表。
	UI_PAGES_MAINT_MENU = 0,
	// 维护确认页面。
	UI_PAGES_MAINT_CONFIRM,
	// 维护调试读数页面。
	UI_PAGES_MAINT_DEBUG
} UiPages_MaintView_t;

typedef struct
{
	uint8_t basket_depth_valid;
	int32_t basket_depth_mm_x10;
	uint8_t tank_depth_valid;
	int32_t tank_depth_mm_x10;
	uint8_t target_depth_valid;
	int32_t target_depth_mm_x10;
	// 运行天数，单位 day；无 RTC，阶段 7 由已保存的上电运行秒数折算，显示范围 001-999。
	uint16_t run_days;
	UiPages_Mode_t mode;
	const char *motion_text;
	const char *notice_text;
	uint8_t next_nap_valid;
	uint32_t next_nap_remaining_s;
	// 当天自动打盹已完成脉冲数，单位 pulse；主页面以 Pxxx 摘要显示。
	uint16_t today_done_pulses;
	uint16_t nap_pulses;
	ErrorCode_t primary_error;
	uint8_t buzzer_muted;
} UiPages_MainContext_t;

typedef struct
{
	uint8_t seconds_left_valid;
	uint16_t seconds_left;
	uint8_t i2c_a_ok;
	uint8_t i2c_b_ok;
	uint8_t i2c_c_ok;
	Limit_State_t limits;
} UiPages_SelfTestContext_t;

typedef struct
{
	uint8_t air_pressure_valid;
	int32_t air_pressure_hpa_x100;
	uint8_t basket_depth_valid;
	int32_t basket_depth_mm_x10;
	uint8_t tank_depth_valid;
	int32_t tank_depth_mm_x10;
	uint16_t i2c_a_failures;
	uint16_t i2c_b_failures;
	uint16_t i2c_c_failures;
} UiPages_SensorContext_t;

typedef struct
{
	Limit_State_t limits;
	uint8_t upper_blocked;
	uint8_t lower_blocked;
	uint8_t position_valid;
	int32_t position_mm_x10;
	uint8_t position_trusted;
	uint8_t limit_mismatch;
} UiPages_LimitContext_t;

typedef struct
{
	UiPages_ParamId_t param_id;
	const ParamStore_Record_t *record;
	uint8_t dirty;
	uint8_t save_error;
} UiPages_ParamContext_t;

typedef struct
{
	UiPages_ManualState_t state;
	uint8_t basket_depth_valid;
	int32_t basket_depth_mm_x10;
	uint8_t position_valid;
	int32_t position_mm_x10;
	uint8_t limit_blocked;
} UiPages_ManualContext_t;

typedef struct
{
	ErrorCode_t primary_error;
	ErrorLevel_t primary_error_level;
	uint8_t active_error_count;
	uint8_t buzzer_muted;
} UiPages_AlarmContext_t;

typedef struct
{
	UiPages_MaintView_t view;
	uint8_t menu_index;
	uint8_t menu_count;
	const char *line2;
	const char *line3;
	uint8_t motor_released;
	uint8_t position_trusted;
	uint8_t homing_busy;
	uint8_t air_reference_valid;
	int32_t air_reference_hpa_x100;
} UiPages_MaintContext_t;

// 函    数：UiPages_RenderFrame
// 参    数：frame 4 行 x 16 字符页面缓存，每行必须以 '\0' 结尾。
// 返 回 值：无
// 注意事项：阻塞时间来自现有 OLED 软件 I2C 写屏；调用方应按 BOARD_UI_REFRESH_MS 周期刷新，不要在中断中调用。
void UiPages_RenderFrame(UiPages_Frame_t frame);

// 函    数：UiPages_FormatMain
// 参    数：ctx 主页面输入上下文；frame 输出 4 行 x 16 字符页面缓存。
// 返 回 值：无
// 注意事项：只做字符串格式化，不读取传感器、不写 OLED。
void UiPages_FormatMain(const UiPages_MainContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatSelfTest
// 参    数：ctx 自检页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：倒计时单位为 second；I2C 状态由调用方转换后传入。
void UiPages_FormatSelfTest(const UiPages_SelfTestContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatSensor
// 参    数：ctx 传感器页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：压力单位 hPa_x100，水深单位 mm_x10；无效数据用占位符显示。
void UiPages_FormatSensor(const UiPages_SensorContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatLimit
// 参    数：ctx 限位/位置页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：只展示 active-low/active-high 已归一化后的限位状态，不执行停机动作。
void UiPages_FormatLimit(const UiPages_LimitContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatParam
// 参    数：ctx 参数页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：只显示当前参数和脏标志，参数范围校验和 Flash 保存由 menu/param_store 负责。
void UiPages_FormatParam(const UiPages_ParamContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatManual
// 参    数：ctx 手动页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：limit_blocked 仅用于提示，实际 STEP 禁止由 app_state/limit 模块执行。
void UiPages_FormatManual(const UiPages_ManualContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatAlarm
// 参    数：ctx 报警页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：蜂鸣器静音只作为显示状态，不代表故障已清除。
void UiPages_FormatAlarm(const UiPages_AlarmContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_FormatMaintenance
// 参    数：ctx 维护页面输入上下文；frame 输出页面缓存。
// 返 回 值：无
// 注意事项：维护确认文案来自调用方，本函数不启动回零、校准或电机释放。
void UiPages_FormatMaintenance(const UiPages_MaintContext_t *ctx, UiPages_Frame_t frame);

// 函    数：UiPages_RenderMain
// 参    数：ctx 主页面输入上下文。
// 返 回 值：无
// 注意事项：内部先格式化再阻塞写 OLED，不要在中断中调用。
void UiPages_RenderMain(const UiPages_MainContext_t *ctx);

// 函    数：UiPages_RenderSelfTest
// 参    数：ctx 自检页面输入上下文。
// 返 回 值：无
// 注意事项：阻塞式刷新 OLED，自检状态推进仍由 self_test/app_state 负责。
void UiPages_RenderSelfTest(const UiPages_SelfTestContext_t *ctx);

// 函    数：UiPages_RenderSensor
// 参    数：ctx 传感器页面输入上下文。
// 返 回 值：无
// 注意事项：只显示最近读数和失败计数，不触发 I2C 重试。
void UiPages_RenderSensor(const UiPages_SensorContext_t *ctx);

// 函    数：UiPages_RenderLimit
// 参    数：ctx 限位/位置页面输入上下文。
// 返 回 值：无
// 注意事项：只渲染状态，限位停机必须由安全逻辑完成。
void UiPages_RenderLimit(const UiPages_LimitContext_t *ctx);

// 函    数：UiPages_RenderParam
// 参    数：ctx 参数页面输入上下文。
// 返 回 值：无
// 注意事项：不会写 Flash；保存动作由菜单确认键触发。
void UiPages_RenderParam(const UiPages_ParamContext_t *ctx);

// 函    数：UiPages_RenderManual
// 参    数：ctx 手动页面输入上下文。
// 返 回 值：无
// 注意事项：只显示长按/限位状态，不直接输出 STEP 脉冲。
void UiPages_RenderManual(const UiPages_ManualContext_t *ctx);

// 函    数：UiPages_RenderAlarm
// 参    数：ctx 报警页面输入上下文。
// 返 回 值：无
// 注意事项：只显示报警和静音状态，不清除 error_manager 锁存。
void UiPages_RenderAlarm(const UiPages_AlarmContext_t *ctx);

// 函    数：UiPages_RenderMaintenance
// 参    数：ctx 维护页面输入上下文。
// 返 回 值：无
// 注意事项：维护动作必须由 app_state 根据菜单意图执行，本函数只写 OLED。
void UiPages_RenderMaintenance(const UiPages_MaintContext_t *ctx);

#endif
