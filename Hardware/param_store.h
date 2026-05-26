#ifndef __PARAM_STORE_H
#define __PARAM_STORE_H

#include "stm32f10x.h"

// 模    块：Flash 参数与恢复状态保存
// 存储位置：STM32F103C8T6 末尾两个 1KB Flash 页，A=0x0800F800，B=0x0800FC00。
// 安全假设：调用保存接口前，上层应确保电机不在输出 STEP；Flash 擦写是阻塞操作。
// 写入策略：参数修改立即保存，运行状态使用 1h 节流，关键状态切换使用强制保存。

typedef enum
{
	// 操作成功。
	PARAM_STORE_STATUS_OK = 0,
	// 参数非法，例如传入空指针。
	PARAM_STORE_STATUS_ERROR_PARAM,
	// Flash 页为空或两页均无有效记录，已可回退默认值。
	PARAM_STORE_STATUS_ERROR_EMPTY,
	// magic/version/crc16 不匹配，记录不可用于恢复。
	PARAM_STORE_STATUS_ERROR_CRC,
	// 记录字段越界，例如目标水深或打盹脉冲数非法。
	PARAM_STORE_STATUS_ERROR_RANGE,
	// Flash 擦除、编程或写后校验失败。
	PARAM_STORE_STATUS_ERROR_FLASH,
	// 运行状态保存被 1h 节流拒绝。
	PARAM_STORE_STATUS_THROTTLED,
	// 已使用默认参数，通常同时置位 W_PARAM_DEFAULT。
	PARAM_STORE_STATUS_DEFAULT_USED
} ParamStore_Status_t;

typedef enum
{
	// 开机自检状态。
	PARAM_STORE_APP_SELF_TEST = 0,
	// 暂停等待人工确认。
	PARAM_STORE_APP_PAUSED,
	// 自动运行判定。
	PARAM_STORE_APP_AUTO_RUN,
	// 等待下一次打盹。
	PARAM_STORE_APP_NAP_WAIT,
	// 正在执行一次有限脉冲打盹。
	PARAM_STORE_APP_NAP_MOVE,
	// 手动操作。
	PARAM_STORE_APP_MANUAL,
	// 维护模式。
	PARAM_STORE_APP_MAINTENANCE,
	// 电机释放状态。
	PARAM_STORE_APP_MOTOR_RELEASE,
	// 故障状态。
	PARAM_STORE_APP_FAULT,
	PARAM_STORE_APP_STATE_COUNT
} ParamStore_AppState_t;

typedef struct
{
	// 固定标识，必须为 BOARD_PARAM_MAGIC，避免误读普通 Flash 内容。
	uint32_t magic;
	// 结构版本，当前为 BOARD_PARAM_VERSION。
	uint16_t version;
	// 保留字段，固定写 0，保证结构按半字对齐。
	uint16_t reserved0;
	// 递增序号，A/B 两页都有效时选择 seq 较新的记录。
	uint32_t seq;
	// 初始目标水深，单位 mm_x10，默认 100.0mm。
	int32_t initial_target_mm_x10;
	// 最终目标水深，单位 mm_x10，默认 10.0mm。
	int32_t final_target_mm_x10;
	// 每日变浅量，单位 mm_x10/day，默认 1.0mm/day。
	int32_t daily_shallow_mm_x10;
	// 单次自动打盹脉冲数，单位 pulse，默认 8 pulse。
	uint16_t nap_pulses;
	// 保留字段，固定写 0。
	uint16_t reserved1;
	// 累计上电运行时间，单位 second；断电期间不累计。
	uint32_t total_run_seconds;
	// 当前运行日已累计上电时间，单位 second。
	uint32_t today_run_seconds;
	// 当前运行日已执行自动脉冲数，单位 pulse。
	uint32_t today_pulses_done;
	// 估算机械位置，单位 pulse，最低机械位为 0。
	int32_t basket_position_pulses;
	// 位置可信标志：1=可信，0=不可信；不可信时禁止自动恢复运行。
	uint8_t position_trusted;
	// 断电前应用状态，取 ParamStore_AppState_t。
	uint8_t last_app_state;
	// 蜂鸣器静音标志：1=静音，0=未静音；静音不清除故障。
	uint8_t buzzer_muted;
	// 保留字段，固定写 0。
	uint8_t reserved2;
	// 最近一次有效鱼缸水深，单位 mm_x10。
	int32_t last_tank_depth_mm_x10;
	// 最近一次有效框篮水深，单位 mm_x10。
	int32_t last_basket_depth_mm_x10;
	// 维护空气零点校准打包值：保存 P_basket-P_air 和 P_tank-P_air 两个偏移，单位 hPa_x100。
	// 注意事项：字段名沿用 v1 Flash 记录，布局和 BOARD_PARAM_VERSION 不变。
	int32_t air_offset_hpa_x100;
	// CRC16 校验值，覆盖本字段之前的记录内容。
	uint16_t crc16;
} ParamStore_Record_t;

// 函    数：ParamStore_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：读取结果；两页无效时返回 PARAM_STORE_STATUS_DEFAULT_USED。
// 注意事项：初始化会读取 A/B 两页并缓存最新有效记录；无效时载入默认值并置 W_PARAM_DEFAULT。
ParamStore_Status_t ParamStore_Init(uint32_t now_ms);

// 函    数：ParamStore_Load
// 参    数：record 输出当前缓存记录。
// 返 回 值：读取状态。
// 注意事项：不会触发 Flash 擦写；调用方必须检查返回值。
ParamStore_Status_t ParamStore_Load(ParamStore_Record_t *record);

// 函    数：ParamStore_LoadDefaults
// 参    数：record 输出默认记录。
// 返 回 值：无
// 注意事项：默认记录 position_trusted=0，不允许断电后未回零自动运行。
void ParamStore_LoadDefaults(ParamStore_Record_t *record);

// 函    数：ParamStore_SaveParameters
// 参    数：record 待保存的配置/状态记录。
// 返 回 值：保存状态。
// 注意事项：用于用户确认参数修改后立即保存；内部会更新 magic/version/seq/crc16。
ParamStore_Status_t ParamStore_SaveParameters(const ParamStore_Record_t *record);

// 函    数：ParamStore_SaveRuntime
// 参    数：record 待保存运行状态；now_ms 当前系统毫秒时间戳。
// 返 回 值：保存状态，未达到 1h 间隔返回 PARAM_STORE_STATUS_THROTTLED。
// 注意事项：禁止每次打盹脉冲后调用本函数造成频繁擦写。
ParamStore_Status_t ParamStore_SaveRuntime(const ParamStore_Record_t *record, uint32_t now_ms);

// 函    数：ParamStore_ForceSaveRuntime
// 参    数：record 待保存运行状态；now_ms 当前系统毫秒时间戳。
// 返 回 值：保存状态。
// 注意事项：仅用于进入暂停、故障、维护、电机释放等关键状态前。
ParamStore_Status_t ParamStore_ForceSaveRuntime(const ParamStore_Record_t *record, uint32_t now_ms);

// 函    数：ParamStore_ShouldSaveRuntime
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：1 表示距离上次运行状态保存已达到 1h，0 表示仍需节流。
uint8_t ParamStore_ShouldSaveRuntime(uint32_t now_ms);

// 函    数：ParamStore_ValidateRecord
// 参    数：record 待检查记录。
// 返 回 值：记录有效返回 OK，否则返回具体错误。
// 注意事项：校验 magic/version/crc16 和关键参数范围，不访问 Flash。
ParamStore_Status_t ParamStore_ValidateRecord(const ParamStore_Record_t *record);

// 函    数：ParamStore_ComputeRecordCrc
// 参    数：record 待计算记录。
// 返 回 值：覆盖 crc16 字段之前内容的 CRC16。
uint16_t ParamStore_ComputeRecordCrc(const ParamStore_Record_t *record);

// 函    数：ParamStore_GetActivePageAddress
// 参    数：无
// 返 回 值：当前缓存记录来自的 Flash 页地址；默认值尚未写入时返回 0。
uint32_t ParamStore_GetActivePageAddress(void);

#endif
