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

#define PARAM_STORE_RECORDS_PER_PAGE \
	((uint16_t)(BOARD_PARAM_FLASH_PAGE_SIZE / sizeof(ParamStore_Record_t)))
#define PARAM_STORE_INVALID_SLOT_INDEX 0xFFFFU

typedef struct
{
	ParamStore_Record_t latest_record;
	uint8_t has_valid_record;
	uint8_t has_empty_slot;
	uint8_t has_damaged_slot;
	uint16_t latest_slot;
	uint16_t first_empty_slot;
} ParamStore_PageScan_t;

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
// 参    数：record Flash 槽位记录副本。
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

// 函    数：ParamStore_GetSlotAddress
// 参    数：page_addr Flash 页起始地址；slot_index 页内记录槽位序号。
// 返 回 值：槽位对应的 Flash 地址。
// 注意事项：槽位大小固定为 ParamStore_Record_t；不新增 Flash 字段。
static uint32_t ParamStore_GetSlotAddress(uint32_t page_addr, uint16_t slot_index)
{
	return page_addr + ((uint32_t)slot_index * (uint32_t)sizeof(ParamStore_Record_t));
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
	if (((uint16_t)sizeof(ParamStore_Record_t) & 1U) != 0U)
	{
		return 0U;
	}
	if (PARAM_STORE_RECORDS_PER_PAGE == 0U)
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

// 函    数：ParamStore_ReadSlot
// 参    数：page_addr 参数页地址；slot_index 页内槽位；record 输出记录副本。
// 返 回 值：记录校验状态。
// 注意事项：兼容旧页首记录；页内其它槽位为空时返回 ERROR_EMPTY。
static ParamStore_Status_t ParamStore_ReadSlot(uint32_t page_addr,
                                               uint16_t slot_index,
                                               ParamStore_Record_t *record)
{
	const ParamStore_Record_t *flash_record;
	uint32_t slot_addr;

	if ((record == 0) || (slot_index >= PARAM_STORE_RECORDS_PER_PAGE))
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	slot_addr = ParamStore_GetSlotAddress(page_addr, slot_index);
#ifdef PARAM_STORE_HOST_TEST
	flash_record = (const ParamStore_Record_t *)(uintptr_t)slot_addr;
#else
	flash_record = (const ParamStore_Record_t *)slot_addr;
#endif
	ParamStore_CopyRecord(record, flash_record);

	if (ParamStore_IsErasedRecord(record) != 0U)
	{
		return PARAM_STORE_STATUS_ERROR_EMPTY;
	}

	return ParamStore_ValidateRecord(record);
}

// 函    数：ParamStore_ScanPage
// 参    数：page_addr 参数页地址；scan 输出页扫描结果。
// 返 回 值：无
// 注意事项：扫描整页固定槽位；CRC/范围错误的槽位只标记损坏，不参与恢复。
static void ParamStore_ScanPage(uint32_t page_addr, ParamStore_PageScan_t *scan)
{
	ParamStore_Record_t candidate;
	ParamStore_Status_t status;
	uint16_t slot;

	if (scan == 0)
	{
		return;
	}

	(void)memset(scan, 0, sizeof(ParamStore_PageScan_t));
	scan->latest_slot = PARAM_STORE_INVALID_SLOT_INDEX;
	scan->first_empty_slot = PARAM_STORE_INVALID_SLOT_INDEX;

	for (slot = 0U; slot < PARAM_STORE_RECORDS_PER_PAGE; slot++)
	{
		status = ParamStore_ReadSlot(page_addr, slot, &candidate);
		if (status == PARAM_STORE_STATUS_OK)
		{
			if ((scan->has_valid_record == 0U) ||
			    ((int32_t)(candidate.seq - scan->latest_record.seq) >= 0))
			{
				ParamStore_CopyRecord(&scan->latest_record, &candidate);
				scan->latest_slot = slot;
			}
			scan->has_valid_record = 1U;
		}
		else if (status == PARAM_STORE_STATUS_ERROR_EMPTY)
		{
			if (scan->has_empty_slot == 0U)
			{
				scan->first_empty_slot = slot;
			}
			scan->has_empty_slot = 1U;
		}
		else
		{
			scan->has_damaged_slot = 1U;
		}
	}
}

// 函    数：ParamStore_ReadLatest
// 参    数：record 输出最新有效记录；page_addr 输出页地址；slot_index 输出页内槽位。
// 返 回 值：读取状态。
// 注意事项：A/B 两页全部槽位都参与 CRC 校验；损坏记录不会覆盖旧有效记录。
static ParamStore_Status_t ParamStore_ReadLatest(ParamStore_Record_t *record,
                                                 uint32_t *page_addr,
                                                 uint16_t *slot_index)
{
	ParamStore_PageScan_t scan_a;
	ParamStore_PageScan_t scan_b;

	if ((record == 0) || (page_addr == 0) || (slot_index == 0))
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	ParamStore_ScanPage(BOARD_PARAM_FLASH_PAGE_A_ADDR, &scan_a);
	ParamStore_ScanPage(BOARD_PARAM_FLASH_PAGE_B_ADDR, &scan_b);

	if ((scan_a.has_valid_record != 0U) && (scan_b.has_valid_record != 0U))
	{
		if ((int32_t)(scan_a.latest_record.seq - scan_b.latest_record.seq) >= 0)
		{
			ParamStore_CopyRecord(record, &scan_a.latest_record);
			*page_addr = BOARD_PARAM_FLASH_PAGE_A_ADDR;
			*slot_index = scan_a.latest_slot;
		}
		else
		{
			ParamStore_CopyRecord(record, &scan_b.latest_record);
			*page_addr = BOARD_PARAM_FLASH_PAGE_B_ADDR;
			*slot_index = scan_b.latest_slot;
		}
		return PARAM_STORE_STATUS_OK;
	}

	if (scan_a.has_valid_record != 0U)
	{
		ParamStore_CopyRecord(record, &scan_a.latest_record);
		*page_addr = BOARD_PARAM_FLASH_PAGE_A_ADDR;
		*slot_index = scan_a.latest_slot;
		return PARAM_STORE_STATUS_OK;
	}

	if (scan_b.has_valid_record != 0U)
	{
		ParamStore_CopyRecord(record, &scan_b.latest_record);
		*page_addr = BOARD_PARAM_FLASH_PAGE_B_ADDR;
		*slot_index = scan_b.latest_slot;
		return PARAM_STORE_STATUS_OK;
	}

	if ((scan_a.has_damaged_slot == 0U) && (scan_b.has_damaged_slot == 0U))
	{
		return PARAM_STORE_STATUS_ERROR_EMPTY;
	}

	return PARAM_STORE_STATUS_ERROR_CRC;
}

// 函    数：ParamStore_GetOtherPage
// 参    数：page_addr 当前活动页地址。
// 返 回 值：另一页地址。
// 注意事项：仅在活动页满、损坏或不能安全追加时切页擦写。
static uint32_t ParamStore_GetOtherPage(uint32_t page_addr)
{
	if (page_addr == BOARD_PARAM_FLASH_PAGE_A_ADDR)
	{
		return BOARD_PARAM_FLASH_PAGE_B_ADDR;
	}

	return BOARD_PARAM_FLASH_PAGE_A_ADDR;
}

// 函    数：ParamStore_GetTargetLocation
// 参    数：page_addr 输出目标页；slot_index 输出槽位；erase_page 输出是否需先擦页。
// 返 回 值：目标选择状态。
// 注意事项：优先追加当前活动页空槽；页满或有损坏槽时写入另一页槽 0。
static ParamStore_Status_t ParamStore_GetTargetLocation(uint32_t *page_addr,
                                                        uint16_t *slot_index,
                                                        uint8_t *erase_page)
{
	ParamStore_PageScan_t active_scan;

	if ((page_addr == 0) || (slot_index == 0) || (erase_page == 0))
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	if ((s_active_page_addr == BOARD_PARAM_FLASH_PAGE_A_ADDR) ||
	    (s_active_page_addr == BOARD_PARAM_FLASH_PAGE_B_ADDR))
	{
		ParamStore_ScanPage(s_active_page_addr, &active_scan);
		if ((active_scan.has_valid_record != 0U) &&
		    (active_scan.has_damaged_slot == 0U) &&
		    (active_scan.has_empty_slot != 0U))
		{
			*page_addr = s_active_page_addr;
			*slot_index = active_scan.first_empty_slot;
			*erase_page = 0U;
			return PARAM_STORE_STATUS_OK;
		}

		*page_addr = ParamStore_GetOtherPage(s_active_page_addr);
		*slot_index = 0U;
		*erase_page = 1U;
		return PARAM_STORE_STATUS_OK;
	}

	*page_addr = BOARD_PARAM_FLASH_PAGE_A_ADDR;
	*slot_index = 0U;
	*erase_page = 1U;
	return PARAM_STORE_STATUS_OK;
}

// 函    数：ParamStore_ErasePage
// 参    数：page_addr 目标页地址。
// 返 回 值：Flash 操作状态。
// 注意事项：只在页面需要滚转或首次保存时擦除；普通追加不擦页。
static ParamStore_Status_t ParamStore_ErasePage(uint32_t page_addr)
{
	FLASH_Status flash_status;

	if (ParamStore_IsFlashLayoutValid() == 0U)
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

	flash_status = FLASH_ErasePage(page_addr);
	if (flash_status != FLASH_COMPLETE)
	{
		FLASH_Lock();
		return PARAM_STORE_STATUS_ERROR_FLASH;
	}

	FLASH_Lock();
	return PARAM_STORE_STATUS_OK;
}

// 函    数：ParamStore_ProgramSlot
// 参    数：page_addr 目标页地址；slot_index 页内槽位；record 待写入记录。
// 返 回 值：Flash 操作状态。
// 注意事项：只写已擦除槽位；A/B 旧有效记录在新槽位校验通过前仍可用于恢复。
static ParamStore_Status_t ParamStore_ProgramSlot(uint32_t page_addr,
                                                  uint16_t slot_index,
                                                  const ParamStore_Record_t *record)
{
	const uint16_t *data;
	uint32_t address;
	uint16_t count;
	uint16_t i;
	FLASH_Status flash_status;

	if ((record == 0) ||
	    (slot_index >= PARAM_STORE_RECORDS_PER_PAGE) ||
	    (ParamStore_IsFlashLayoutValid() == 0U))
	{
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	data = (const uint16_t *)record;
	count = (uint16_t)(sizeof(ParamStore_Record_t) / 2U);
	address = ParamStore_GetSlotAddress(page_addr, slot_index);

	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

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
// 注意事项：公共保存接口的共同入口，统一处理 seq、CRC、页内追加/切页滚转和写后校验。
static ParamStore_Status_t ParamStore_SaveInternal(const ParamStore_Record_t *record,
                                                   uint32_t now_ms,
                                                   uint8_t update_runtime_time)
{
	ParamStore_Record_t candidate;
	ParamStore_Record_t verify;
	ParamStore_Status_t status;
	uint32_t target_page;
	uint16_t target_slot;
	uint8_t erase_page;

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

	status = ParamStore_GetTargetLocation(&target_page, &target_slot, &erase_page);
	if (status != PARAM_STORE_STATUS_OK)
	{
		return status;
	}

	if (erase_page != 0U)
	{
		status = ParamStore_ErasePage(target_page);
		if (status != PARAM_STORE_STATUS_OK)
		{
			return status;
		}
	}

	status = ParamStore_ProgramSlot(target_page, target_slot, &candidate);
	if (status != PARAM_STORE_STATUS_OK)
	{
		return status;
	}

	status = ParamStore_ReadSlot(target_page, target_slot, &verify);
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
	uint16_t slot_index;

	page_addr = 0UL;
	slot_index = PARAM_STORE_INVALID_SLOT_INDEX;
	if (ParamStore_IsFlashLayoutValid() == 0U)
	{
		ParamStore_LoadDefaults(&s_cached_record);
		s_initialized = 1U;
		s_active_page_addr = 0UL;
		s_next_seq = 1UL;
		s_last_runtime_save_ms = now_ms;
		return PARAM_STORE_STATUS_ERROR_PARAM;
	}

	status = ParamStore_ReadLatest(&s_cached_record, &page_addr, &slot_index);
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
// 注意事项：参数修改确认后立即保存，不受 1h 运行状态节流限制。
ParamStore_Status_t ParamStore_SaveParameters(const ParamStore_Record_t *record)
{
	return ParamStore_SaveInternal(record, s_last_runtime_save_ms, 0U);
}

// 函    数：ParamStore_SaveRuntime
// 参    数：record 待保存运行状态；now_ms 当前系统毫秒时间戳。
// 返 回 值：保存状态。
// 注意事项：普通运行状态最多每 1h 保存一次，避免每次打盹都擦写 Flash。
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
// 返 回 值：达到 1h 保存间隔返回 1，否则返回 0。
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
