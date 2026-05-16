#include "param_store.h"
#include "board_config.h"
#include "crc16.h"
#include "error_manager.h"
#include "stm32f10x_flash.h"
#include <stddef.h>
#include <string.h>

static ParamStore_Record_t s_cached_record;
static uint8_t s_initialized;
static uint32_t s_active_page_addr;
static uint32_t s_next_seq;
static uint32_t s_last_runtime_save_ms;

// 函    数：ParamStore_IsTimeElapsed
// 参    数：now_ms 当前时间；last_ms 上次时间；interval_ms 间隔。
// 返 回 值：达到间隔返回 1，否则返回 0。
// 注意事项：使用无符号差值，允许系统毫秒计数回绕。
static uint8_t ParamStore_IsTimeElapsed(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
	return ((uint32_t)(now_ms - last_ms) >= interval_ms) ? 1U : 0U;
}

// 函    数：ParamStore_CopyRecord
// 参    数：dest 目标记录；src 源记录。
// 返 回 值：无
// 注意事项：结构体中保留字段会被整体复制，保存前会重新规范化。
static void ParamStore_CopyRecord(ParamStore_Record_t *dest, const ParamStore_Record_t *src)
{
	(void)memcpy(dest, src, sizeof(ParamStore_Record_t));
}

// 函    数：ParamStore_IsErasedRecord
// 参    数：record Flash 页首部记录副本。
// 返 回 值：全 0xFF 返回 1，否则返回 0。
// 注意事项：用于区分空白参数页和 CRC 损坏页。
static uint8_t ParamStore_IsErasedRecord(const ParamStore_Record_t *record)
{
	const uint8_t *bytes;
	uint16_t i;

	if (record == 0)
	{
		return 0U;
	}

	bytes = (const uint8_t *)record;
	for (i = 0U; i < (uint16_t)sizeof(ParamStore_Record_t); i++)
	{
		if (bytes[i] != 0xFFU)
		{
			return 0U;
		}
	}

	return 1U;
}

// 函    数：ParamStore_NormalizeRecord
// 参    数：record 待规范化记录；seq 本次保存序号。
// 返 回 值：无
// 注意事项：写 Flash 前固定 magic/version/seq/保留字段并重新计算 CRC。
static void ParamStore_NormalizeRecord(ParamStore_Record_t *record, uint32_t seq)
{
	record->magic = BOARD_PARAM_MAGIC;
	record->version = BOARD_PARAM_VERSION;
	record->reserved0 = 0U;
	record->seq = seq;
	record->reserved1 = 0U;
	record->position_trusted = (record->position_trusted != 0U) ? 1U : 0U;
	record->buzzer_muted = (record->buzzer_muted != 0U) ? 1U : 0U;
	record->reserved2 = 0U;
	record->crc16 = ParamStore_ComputeRecordCrc(record);
}

// 函    数：ParamStore_IsFlashLayoutValid
// 参    数：无
// 返 回 值：Flash 地址布局符合阶段 6 预留要求返回 1，否则返回 0。
// 注意事项：防止 IROM 或页地址配置错误时继续擦写代码区。
static uint8_t ParamStore_IsFlashLayoutValid(void)
{
	if (BOARD_IROM_END_ADDR > BOARD_PARAM_FLASH_PAGE_A_ADDR)
	{
		return 0U;
	}
	if ((BOARD_PARAM_FLASH_PAGE_A_ADDR + BOARD_PARAM_FLASH_PAGE_SIZE) != BOARD_PARAM_FLASH_PAGE_B_ADDR)
	{
		return 0U;
	}
	if ((BOARD_PARAM_FLASH_PAGE_B_ADDR + BOARD_PARAM_FLASH_PAGE_SIZE) >
	    (BOARD_FLASH_BASE_ADDR + BOARD_FLASH_TOTAL_SIZE_BYTES))
	{
		return 0U;
	}
	if (sizeof(ParamStore_Record_t) > BOARD_PARAM_FLASH_PAGE_SIZE)
	{
		return 0U;
	}

	return 1U;
}

// 函    数：ParamStore_IsNapIntervalValid
// 参    数：daily_shallow_mm_x10 每日变浅量，单位 mm_x10/day；nap_pulses 单次打盹脉冲数，单位 pulse。
// 返 回 值：组合参数不会让理论打盹间隔低于 BOARD_NAP_MIN_INTERVAL_MS 时返回 1。
// 注意事项：保存层再次校验菜单组合，防止 3.0mm/day + 8 pulse 等参数绕过菜单后写入 Flash。
static uint8_t ParamStore_IsNapIntervalValid(int32_t daily_shallow_mm_x10, uint16_t nap_pulses)
{
	uint32_t daily_pulses;
	uint32_t interval_ms;

	if (daily_shallow_mm_x10 <= 0L)
	{
		return 1U;
	}
	if (nap_pulses == 0U)
	{
		return 0U;
	}

	daily_pulses = ((uint32_t)daily_shallow_mm_x10 * BOARD_STEPPER_PULSE_PER_MM) / 10UL;
	if (daily_pulses == 0UL)
	{
		return 1U;
	}

	interval_ms = ((BOARD_SECONDS_PER_DAY * 1000UL) * (uint32_t)nap_pulses) / daily_pulses;
	return (interval_ms >= BOARD_NAP_MIN_INTERVAL_MS) ? 1U : 0U;
}

// 函    数：ParamStore_ReadPage
// 参    数：page_addr 参数页地址；record 输出记录副本。
// 返 回 值：记录校验状态。
// 注意事项：只读取页首一条记录；A/B 页通过 seq 选择最新有效记录。
static ParamStore_Status_t ParamStore_ReadPage(uint32_t page_addr, ParamStore_Record_t *record)
{
	const ParamStore_Record_t *flash_record;

	if (record == 0)
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

#ifdef PARAM_STORE_HOST_TEST
	flash_record = (const ParamStore_Record_t *)(uintptr_t)page_addr;
#else
	flash_record = (const ParamStore_Record_t *)page_addr;
#endif
	ParamStore_CopyRecord(record, flash_record);

	if (ParamStore_IsErasedRecord(record) != 0U)
	{
		return PARAM_STORE_STATUS_ERROR_EMPTY;
	}

	return ParamStore_ValidateRecord(record);
}

// 函    数：ParamStore_ReadLatest
// 参    数：record 输出最新有效记录；page_addr 输出有效记录所在页地址。
// 返 回 值：读取状态。
// 注意事项：两页都有效时按 seq 选择较新记录；一页损坏不会影响另一页恢复。
static ParamStore_Status_t ParamStore_ReadLatest(ParamStore_Record_t *record, uint32_t *page_addr)
{
	ParamStore_Record_t page_a;
	ParamStore_Record_t page_b;
	ParamStore_Status_t status_a;
	ParamStore_Status_t status_b;

	status_a = ParamStore_ReadPage(BOARD_PARAM_FLASH_PAGE_A_ADDR, &page_a);
	status_b = ParamStore_ReadPage(BOARD_PARAM_FLASH_PAGE_B_ADDR, &page_b);

	if ((status_a == PARAM_STORE_STATUS_OK) && (status_b == PARAM_STORE_STATUS_OK))
	{
		if ((int32_t)(page_a.seq - page_b.seq) >= 0)
		{
			ParamStore_CopyRecord(record, &page_a);
			*page_addr = BOARD_PARAM_FLASH_PAGE_A_ADDR;
		}
		else
		{
			ParamStore_CopyRecord(record, &page_b);
			*page_addr = BOARD_PARAM_FLASH_PAGE_B_ADDR;
		}
		return PARAM_STORE_STATUS_OK;
	}

	if (status_a == PARAM_STORE_STATUS_OK)
	{
		ParamStore_CopyRecord(record, &page_a);
		*page_addr = BOARD_PARAM_FLASH_PAGE_A_ADDR;
		return PARAM_STORE_STATUS_OK;
	}

	if (status_b == PARAM_STORE_STATUS_OK)
	{
		ParamStore_CopyRecord(record, &page_b);
		*page_addr = BOARD_PARAM_FLASH_PAGE_B_ADDR;
		return PARAM_STORE_STATUS_OK;
	}

	if ((status_a == PARAM_STORE_STATUS_ERROR_EMPTY) && (status_b == PARAM_STORE_STATUS_ERROR_EMPTY))
	{
		return PARAM_STORE_STATUS_ERROR_EMPTY;
	}

	return PARAM_STORE_STATUS_ERROR_CRC;
}

// 函    数：ParamStore_GetTargetPage
// 参    数：无
// 返 回 值：本次写入目标页。
// 注意事项：始终写入非当前活动页，断电半写入时保留旧页有效记录。
static uint32_t ParamStore_GetTargetPage(void)
{
	if (s_active_page_addr == BOARD_PARAM_FLASH_PAGE_A_ADDR)
	{
		return BOARD_PARAM_FLASH_PAGE_B_ADDR;
	}

	return BOARD_PARAM_FLASH_PAGE_A_ADDR;
}

// 函    数：ParamStore_ProgramRecord
// 参    数：page_addr 目标页地址；record 待写入记录。
// 返 回 值：Flash 操作状态。
// 注意事项：先擦除目标页，再按 halfword 写入；旧活动页在写入完成前不擦除。
static ParamStore_Status_t ParamStore_ProgramRecord(uint32_t page_addr, const ParamStore_Record_t *record)
{
	const uint16_t *data;
	uint32_t address;
	uint16_t count;
	uint16_t i;
	FLASH_Status flash_status;

	if ((record == 0) || (ParamStore_IsFlashLayoutValid() == 0U))
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	data = (const uint16_t *)record;
	count = (uint16_t)(sizeof(ParamStore_Record_t) / 2U);

	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

	flash_status = FLASH_ErasePage(page_addr);
	if (flash_status != FLASH_COMPLETE)
	{
		FLASH_Lock();
		return PARAM_STORE_STATUS_ERROR_FLASH;
	}

	address = page_addr;
	for (i = 0U; i < count; i++)
	{
		flash_status = FLASH_ProgramHalfWord(address, data[i]);
		if (flash_status != FLASH_COMPLETE)
		{
			FLASH_Lock();
			return PARAM_STORE_STATUS_ERROR_FLASH;
		}
		address += 2UL;
	}

	FLASH_Lock();
	return PARAM_STORE_STATUS_OK;
}

// 函    数：ParamStore_SaveInternal
// 参    数：record 待保存记录；now_ms 当前时间；update_runtime_time 非 0 时刷新运行保存时间戳。
// 返 回 值：保存状态。
// 注意事项：公共保存接口的共同入口，统一处理 seq、CRC、A/B 页切换和写后校验。
static ParamStore_Status_t ParamStore_SaveInternal(const ParamStore_Record_t *record,
                                                   uint32_t now_ms,
                                                   uint8_t update_runtime_time)
{
	ParamStore_Record_t candidate;
	ParamStore_Record_t verify;
	ParamStore_Status_t status;
	uint32_t target_page;

	if (record == 0)
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	if (s_initialized == 0U)
	{
		(void)ParamStore_Init(now_ms);
	}

	ParamStore_CopyRecord(&candidate, record);
	ParamStore_NormalizeRecord(&candidate, s_next_seq);

	status = ParamStore_ValidateRecord(&candidate);
	if (status != PARAM_STORE_STATUS_OK)
	{
		ErrorManager_Set(ERROR_CODE_W_PARAM_REJECTED);
		return status;
	}

	target_page = ParamStore_GetTargetPage();
	status = ParamStore_ProgramRecord(target_page, &candidate);
	if (status != PARAM_STORE_STATUS_OK)
	{
		return status;
	}

	status = ParamStore_ReadPage(target_page, &verify);
	if (status != PARAM_STORE_STATUS_OK)
	{
		return PARAM_STORE_STATUS_ERROR_FLASH;
	}

	ParamStore_CopyRecord(&s_cached_record, &verify);
	s_active_page_addr = target_page;
	s_next_seq = verify.seq + 1UL;
	if (update_runtime_time != 0U)
	{
		s_last_runtime_save_ms = now_ms;
	}

	return PARAM_STORE_STATUS_OK;
}

// 函    数：ParamStore_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：读取结果；两页无效时返回 PARAM_STORE_STATUS_DEFAULT_USED。
// 注意事项：无有效记录时只加载默认值，不立即擦写 Flash，避免上电反复消耗寿命。
ParamStore_Status_t ParamStore_Init(uint32_t now_ms)
{
	ParamStore_Status_t status;
	uint32_t page_addr;

	page_addr = 0UL;
	if (ParamStore_IsFlashLayoutValid() == 0U)
	{
		ParamStore_LoadDefaults(&s_cached_record);
		s_initialized = 1U;
		s_active_page_addr = 0UL;
		s_next_seq = 1UL;
		s_last_runtime_save_ms = now_ms;
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	status = ParamStore_ReadLatest(&s_cached_record, &page_addr);
	if (status == PARAM_STORE_STATUS_OK)
	{
		s_active_page_addr = page_addr;
		s_next_seq = s_cached_record.seq + 1UL;
		s_initialized = 1U;
		s_last_runtime_save_ms = now_ms;
		return PARAM_STORE_STATUS_OK;
	}

	ParamStore_LoadDefaults(&s_cached_record);
	s_active_page_addr = 0UL;
	s_next_seq = 1UL;
	s_initialized = 1U;
	s_last_runtime_save_ms = now_ms;
	ErrorManager_Set(ERROR_CODE_W_PARAM_DEFAULT);

	return PARAM_STORE_STATUS_DEFAULT_USED;
}

// 函    数：ParamStore_Load
// 参    数：record 输出当前缓存记录。
// 返 回 值：读取状态。
// 注意事项：未初始化时先执行 ParamStore_Init(0)，但不写 Flash。
ParamStore_Status_t ParamStore_Load(ParamStore_Record_t *record)
{
	if (record == 0)
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	if (s_initialized == 0U)
	{
		(void)ParamStore_Init(0UL);
	}

	ParamStore_CopyRecord(record, &s_cached_record);
	return ParamStore_ValidateRecord(record);
}

// 函    数：ParamStore_LoadDefaults
// 参    数：record 输出默认记录。
// 返 回 值：无
// 注意事项：默认状态为暂停且位置不可信，防止断电后追赶或直接自动运行。
void ParamStore_LoadDefaults(ParamStore_Record_t *record)
{
	if (record == 0)
	{
		return;
	}

	(void)memset(record, 0, sizeof(ParamStore_Record_t));
	record->magic = BOARD_PARAM_MAGIC;
	record->version = BOARD_PARAM_VERSION;
	record->seq = 0UL;
	record->initial_target_mm_x10 = BOARD_TARGET_DEFAULT_INITIAL_MM_X10;
	record->final_target_mm_x10 = BOARD_TARGET_DEFAULT_FINAL_MM_X10;
	record->daily_shallow_mm_x10 = BOARD_DAILY_SHALLOW_DEFAULT_MM_X10;
	record->nap_pulses = BOARD_NAP_DEFAULT_PULSES;
	record->total_run_seconds = 0UL;
	record->today_run_seconds = 0UL;
	record->today_pulses_done = 0UL;
	record->basket_position_pulses = 0L;
	record->position_trusted = 0U;
	record->last_app_state = PARAM_STORE_APP_PAUSED;
	record->buzzer_muted = 0U;
	record->last_tank_depth_mm_x10 = 0L;
	record->last_basket_depth_mm_x10 = 0L;
	record->air_offset_hpa_x100 = 0L;
	record->crc16 = ParamStore_ComputeRecordCrc(record);
}

// 函    数：ParamStore_SaveParameters
// 参    数：record 待保存的配置/状态记录。
// 返 回 值：保存状态。
// 注意事项：参数修改确认后立即保存，不受 10min 运行状态节流限制。
ParamStore_Status_t ParamStore_SaveParameters(const ParamStore_Record_t *record)
{
	return ParamStore_SaveInternal(record, s_last_runtime_save_ms, 0U);
}

// 函    数：ParamStore_SaveRuntime
// 参    数：record 待保存运行状态；now_ms 当前系统毫秒时间戳。
// 返 回 值：保存状态。
// 注意事项：运行状态最多每 10min 保存一次，避免每次打盹都擦写 Flash。
ParamStore_Status_t ParamStore_SaveRuntime(const ParamStore_Record_t *record, uint32_t now_ms)
{
	if (ParamStore_ShouldSaveRuntime(now_ms) == 0U)
	{
		return PARAM_STORE_STATUS_THROTTLED;
	}

	return ParamStore_SaveInternal(record, now_ms, 1U);
}

// 函    数：ParamStore_ForceSaveRuntime
// 参    数：record 待保存运行状态；now_ms 当前系统毫秒时间戳。
// 返 回 值：保存状态。
// 注意事项：关键状态切换前调用，例如暂停、故障、维护、电机释放。
ParamStore_Status_t ParamStore_ForceSaveRuntime(const ParamStore_Record_t *record, uint32_t now_ms)
{
	return ParamStore_SaveInternal(record, now_ms, 1U);
}

// 函    数：ParamStore_ShouldSaveRuntime
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：达到 10min 保存间隔返回 1，否则返回 0。
uint8_t ParamStore_ShouldSaveRuntime(uint32_t now_ms)
{
	if (s_initialized == 0U)
	{
		return 1U;
	}

	return ParamStore_IsTimeElapsed(now_ms,
	                                s_last_runtime_save_ms,
	                                BOARD_PARAM_RUNTIME_SAVE_MS);
}

// 函    数：ParamStore_ValidateRecord
// 参    数：record 待检查记录。
// 返 回 值：记录有效返回 OK，否则返回具体错误。
// 注意事项：范围校验使用 mm_x10、pulse、second 等固定整数单位。
ParamStore_Status_t ParamStore_ValidateRecord(const ParamStore_Record_t *record)
{
	if (record == 0)
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	if (ParamStore_IsErasedRecord(record) != 0U)
	{
		return PARAM_STORE_STATUS_ERROR_EMPTY;
	}

	if ((record->magic != BOARD_PARAM_MAGIC) ||
	    (record->version != BOARD_PARAM_VERSION) ||
	    (record->crc16 != ParamStore_ComputeRecordCrc(record)))
	{
		return PARAM_STORE_STATUS_ERROR_CRC;
	}

	if ((record->initial_target_mm_x10 < BOARD_TARGET_MIN_DEPTH_MM_X10) ||
	    (record->initial_target_mm_x10 > BOARD_TARGET_MAX_DEPTH_MM_X10) ||
	    (record->final_target_mm_x10 < BOARD_TARGET_MIN_DEPTH_MM_X10) ||
	    (record->final_target_mm_x10 > BOARD_TARGET_MAX_DEPTH_MM_X10) ||
	    (record->final_target_mm_x10 > record->initial_target_mm_x10))
	{
		return PARAM_STORE_STATUS_ERROR_RANGE;
	}

	if ((record->daily_shallow_mm_x10 < 0L) ||
	    (record->daily_shallow_mm_x10 > BOARD_DAILY_SHALLOW_MAX_MM_X10))
	{
		return PARAM_STORE_STATUS_ERROR_RANGE;
	}

	if ((record->nap_pulses == 0U) ||
	    (record->nap_pulses > BOARD_NAP_MAX_PULSES))
	{
		return PARAM_STORE_STATUS_ERROR_RANGE;
	}
	if (ParamStore_IsNapIntervalValid(record->daily_shallow_mm_x10, record->nap_pulses) == 0U)
	{
		return PARAM_STORE_STATUS_ERROR_RANGE;
	}

	if ((record->position_trusted > 1U) ||
	    (record->buzzer_muted > 1U) ||
	    (record->last_app_state >= PARAM_STORE_APP_STATE_COUNT))
	{
		return PARAM_STORE_STATUS_ERROR_RANGE;
	}

	if (record->position_trusted != 0U)
	{
		if ((record->basket_position_pulses < 0L) ||
		    (record->basket_position_pulses > (int32_t)BOARD_BASKET_MAX_POSITION_PULSES))
		{
			return PARAM_STORE_STATUS_ERROR_RANGE;
		}
	}

	return PARAM_STORE_STATUS_OK;
}

// 函    数：ParamStore_ComputeRecordCrc
// 参    数：record 待计算记录。
// 返 回 值：CRC16 校验值。
// 注意事项：crc16 字段位于结构末尾，CRC 覆盖它之前的所有字节。
uint16_t ParamStore_ComputeRecordCrc(const ParamStore_Record_t *record)
{
	uint16_t length;

	if (record == 0)
	{
		return 0U;
	}

	length = (uint16_t)offsetof(ParamStore_Record_t, crc16);
	return CRC16_CcittFalse((const uint8_t *)record, length);
}

// 函    数：ParamStore_GetActivePageAddress
// 参    数：无
// 返 回 值：当前缓存记录来自的 Flash 页地址；默认值尚未写入时返回 0。
uint32_t ParamStore_GetActivePageAddress(void)
{
	return s_active_page_addr;
}
