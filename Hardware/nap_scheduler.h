#ifndef __NAP_SCHEDULER_H
#define __NAP_SCHEDULER_H

#include "stm32f10x.h"
#include "param_store.h"
#include "stepper_um244.h"

// 模    块：自动打盹调度器
// 职    责：根据 Flash 参数计算每日脉冲预算、下一次打盹时间和显示用倒计时。
// 安全约束：本模块只计算调度、预算和卡滞趋势；STEP 输出必须由 app_state 调用步进底层。

typedef struct
{
	// 当前目标水深，单位 mm_x10。
	int32_t target_depth_mm_x10;
	// 单次打盹脉冲数，单位 pulse。
	uint16_t nap_pulses;
	// 今日已完成自动脉冲数，单位 pulse。
	uint16_t today_done_pulses;
	// 下次打盹剩余时间，单位 second。
	uint32_t next_nap_remaining_s;
	// 1 表示倒计时有效，0 表示暂停/故障/维护等不显示倒计时。
	uint8_t next_nap_valid;
} NapScheduler_Display_t;

typedef enum
{
	// 每日计划打盹向上变浅，计入 today_pulses_done。
	MOTION_SOURCE_DAILY_SHALLOW = 0,
	// 低频闭环向上修正，计入 today_pulses_done 并受当天剩余额度限制。
	MOTION_SOURCE_DEPTH_TRACK_UP,
	// 低频闭环向下补深，不计入 today_pulses_done。
	MOTION_SOURCE_DEPTH_TRACK_DOWN,
	// 快速掉水跟随，只允许下降，不计入 today_pulses_done。
	MOTION_SOURCE_DROP_FOLLOW,
	// 手动点动，不计入自动每日变浅进度。
	MOTION_SOURCE_MANUAL,
	// 维护回零，不计入自动每日变浅进度。
	MOTION_SOURCE_HOMING
} MotionSource_t;

typedef enum
{
	// 本次打盹记录正常，未触发卡滞判断或卡滞计数未达故障阈值。
	NAP_SCHEDULER_RECORD_OK = 0,
	// 本次累计达到 1mm 并完成趋势检查，上层可保存一次运行状态。
	NAP_SCHEDULER_RECORD_CHECKPOINT,
	// 连续卡滞嫌疑达到阈值，上层必须置位 E_STALL 并停止自动运动。
	NAP_SCHEDULER_RECORD_STALL_FAULT
} NapScheduler_RecordResult_t;

// 函    数：NapScheduler_Init
// 参    数：record 当前参数/恢复记录；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：按参数计算下一次打盹时间；不会启动电机。
void NapScheduler_Init(const ParamStore_Record_t *record, uint32_t now_ms);

// 函    数：NapScheduler_UpdateConfig
// 参    数：record 当前参数/恢复记录；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：参数变更或重新加载 Flash 后调用，刷新间隔和显示字段。
void NapScheduler_UpdateConfig(const ParamStore_Record_t *record, uint32_t now_ms);

// 函    数：NapScheduler_SyncRuntime
// 参    数：record 当前运行记录。
// 返 回 值：无
// 注意事项：只同步运行秒数和今日 pulse 显示，不重排 next_nap_ms。
void NapScheduler_SyncRuntime(const ParamStore_Record_t *record);

// 函    数：NapScheduler_GetDisplay
// 参    数：now_ms 当前系统毫秒时间戳；auto_wait_enabled 非 0 时显示下一次打盹倒计时；out 输出结构体。
// 返 回 值：无
// 注意事项：只返回显示数据；是否启动运动由 app_state 根据安全条件判断。
void NapScheduler_GetDisplay(uint32_t now_ms, uint8_t auto_wait_enabled, NapScheduler_Display_t *out);

// 函    数：NapScheduler_GetTargetDepthMmX10
// 参    数：无
// 返 回 值：当前目标水深，单位 mm_x10。
// 注意事项：按累计上电运行秒数和每日变浅量计算，最小不低于最终目标水深。
int32_t NapScheduler_GetTargetDepthMmX10(void);

// 函    数：NapScheduler_HasDailyBudget
// 参    数：无
// 返 回 值：1 表示今天仍有自动打盹脉冲额度，0 表示当天计划已完成。
uint8_t NapScheduler_HasDailyBudget(void);

// 函    数：NapScheduler_GetDailyRemainingPulses
// 参    数：无
// 返 回 值：今日自动变浅剩余 pulse 额度。
// 注意事项：自动打盹和低频闭环向上修正共享该额度。
uint32_t NapScheduler_GetDailyRemainingPulses(void);

// 函    数：NapScheduler_IsDue
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：1 表示到达下一次打盹时间且仍有今日额度。
uint8_t NapScheduler_IsDue(uint32_t now_ms);

// 函    数：NapScheduler_GetNextPulses
// 参    数：无
// 返 回 值：本次应输出的打盹脉冲数，单位 pulse。
// 注意事项：返回值不会超过单次配置和今日剩余额度。
uint16_t NapScheduler_GetNextPulses(void);

// 函    数：NapScheduler_RecordMove
// 参    数：record 运行记录；direction 实际运动方向；pulses 完成脉冲数；
//           before_depth_mm_x10/after_depth_mm_x10 运动前后框篮水深。
// 返 回 值：记录结果，用于判断是否需要保存或进入 E_STALL。
// 注意事项：只更新内存记录和调度器缓存，不直接写 Flash。
NapScheduler_RecordResult_t NapScheduler_RecordMove(ParamStore_Record_t *record,
                                                    MotionSource_t source,
                                                    StepperUM244_Direction_t direction,
                                                    uint16_t pulses,
                                                    int32_t before_depth_mm_x10,
                                                    int32_t after_depth_mm_x10,
                                                    uint32_t now_ms);

// 函    数：NapScheduler_OnDayRollover
// 参    数：record 运行记录；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：上电运行日切换后清零当天进度，并重新安排下一次打盹。
void NapScheduler_OnDayRollover(ParamStore_Record_t *record, uint32_t now_ms);

// 函    数：NapScheduler_ResetNext
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：自动恢复或完成一次运动后调用，不追赶断电或暂停期间错过的打盹。
void NapScheduler_ResetNext(uint32_t now_ms);

// 函    数：NapScheduler_GetIntervalMs
// 参    数：无
// 返 回 值：当前打盹间隔，单位 ms。
// 注意事项：结果至少为 BOARD_NAP_MIN_INTERVAL_MS，防止参数导致过频繁唤醒。
uint32_t NapScheduler_GetIntervalMs(void);

#endif
