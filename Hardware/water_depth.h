#ifndef __WATER_DEPTH_H
#define __WATER_DEPTH_H

#include "stm32f10x.h"
#include "error_code.h"

// 模    块：水深计算与传感器健康监测
// 输入单位：WF5805F 绝对压力读数，单位 hPa_x100。
// 输出单位：框篮和鱼缸水深，单位 mm_x10。
// 安全约束：传感器/I2C 连续失败、水位越界、物理异常和水位突变都会置位错误管理器。

typedef enum
{
	// 当前水深状态有效。
	WATER_DEPTH_OK = 0,
	// 尚未收集到三颗传感器的有效滤波读数。
	WATER_DEPTH_PENDING,
	// 调用参数非法。
	WATER_DEPTH_ERROR_PARAM
} WaterDepth_Status_t;

typedef struct
{
	// 三颗传感器的滤波后绝对压力，单位 hPa_x100。
	int32_t air_pressure_hpa_x100;
	int32_t basket_pressure_hpa_x100;
	int32_t tank_pressure_hpa_x100;
	// 差压换算水深，单位 mm_x10。
	int32_t basket_depth_mm_x10;
	int32_t tank_depth_mm_x10;
	// 1 表示三路压力均已有有效滤波平均值。
	uint8_t valid;
	// 状态更新时间，单位 ms。
	uint32_t timestamp_ms;
} WaterDepth_State_t;

typedef struct
{
	// 1 表示趋势数据满足窗口和最小样本数要求。
	uint8_t valid;
	// 参与趋势计算的样本数量。
	uint8_t sample_count;
	// 首末样本时间差，单位 ms。
	uint32_t elapsed_ms;
	// 趋势起点和终点水深，单位 mm_x10。
	int32_t oldest_basket_depth_mm_x10;
	int32_t newest_basket_depth_mm_x10;
	int32_t oldest_tank_depth_mm_x10;
	int32_t newest_tank_depth_mm_x10;
	// 正值表示水深下降速度，单位 mm_x10/min。
	int32_t basket_drop_rate_mm_x10_per_min;
	int32_t tank_drop_rate_mm_x10_per_min;
} WaterDepth_Trend_t;

// 函    数：WaterDepth_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 5 点压力滤波器和水深状态，初始状态为未有效。
void WaterDepth_Init(void);

// 函    数：WaterDepth_SetBasketMotionActive
// 参    数：active 非 0 表示 STEP 底层正在执行命令运动，0 表示框篮静止。
// 返 回 值：无
// 注意事项：只用于抑制“命令运动导致的框篮水深变化”误报；鱼缸水位突变和危险掉水仍照常报警。
void WaterDepth_SetBasketMotionActive(uint8_t active);

// 函    数：WaterDepth_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：轮询传感器读数、更新滤波、计算水深并置位安全错误；无效读数不进滤波。
void WaterDepth_Update(uint32_t now_ms);

// 函    数：WaterDepth_GetState
// 参    数：state 输出最近一次水深状态的结构体指针。
// 返 回 值：WATER_DEPTH_OK 表示状态有效；未有效返回 WATER_DEPTH_PENDING；参数非法返回错误。
// 注意事项：调用方必须检查返回值，不能使用未有效水深做控制判断。
WaterDepth_Status_t WaterDepth_GetState(WaterDepth_State_t *state);

// 函    数：WaterDepth_GetTrend
// 参    数：now_ms 当前系统毫秒时间戳；window_ms 趋势窗口；min_samples 最小有效样本数；trend 输出趋势。
// 返 回 值：WATER_DEPTH_OK 表示趋势有效；样本不足返回 PENDING；参数非法返回 ERROR_PARAM。
// 注意事项：趋势用于快速掉水跟随，正下降速度表示水深变浅或鱼缸水位下降。
WaterDepth_Status_t WaterDepth_GetTrend(uint32_t now_ms,
                                        uint32_t window_ms,
                                        uint8_t min_samples,
                                        WaterDepth_Trend_t *trend);

// 函    数：WaterDepth_ConvertPressureDiffToMmX10
// 参    数：diff_hpa_x100 压力差，单位 hPa_x100。
// 返 回 值：水深，单位 mm_x10。
// 注意事项：只做物理单位换算，不扣除维护模式空气零点；用于显示校准偏移等纯差压场景。
int32_t WaterDepth_ConvertPressureDiffToMmX10(int32_t diff_hpa_x100);

// 函    数：WaterDepth_SetZeroOffsets
// 参    数：basket_offset_hpa_x100 框篮传感器在空气中相对 P_air 的零点偏移，单位 hPa_x100。
// 参    数：tank_offset_hpa_x100 鱼缸传感器在空气中相对 P_air 的零点偏移，单位 hPa_x100。
// 返 回 值：无
// 注意事项：维护模式三颗传感器同处空气时设置；后续水深计算会先扣除该固定偏移。
void WaterDepth_SetZeroOffsets(int32_t basket_offset_hpa_x100,
                               int32_t tank_offset_hpa_x100);

// 函    数：WaterDepth_PackZeroOffsets
// 参    数：basket_offset_hpa_x100 框篮空气零点偏移，单位 hPa_x100，范围 -8192..8191。
// 参    数：tank_offset_hpa_x100 鱼缸空气零点偏移，单位 hPa_x100，范围 -8192..8191。
// 参    数：packed 输出打包值，用于复用 Flash 记录中的 air_offset_hpa_x100 字段。
// 返 回 值：1 表示打包成功，0 表示参数为空或偏移超出可保存范围。
// 注意事项：打包值带标记位，旧版单空气压力记录不会被误识别为有效零点。
uint8_t WaterDepth_PackZeroOffsets(int32_t basket_offset_hpa_x100,
                                   int32_t tank_offset_hpa_x100,
                                   int32_t *packed);

// 函    数：WaterDepth_UnpackZeroOffsets
// 参    数：packed Flash 中保存的打包值。
// 参    数：basket_offset_hpa_x100 输出框篮空气零点偏移，单位 hPa_x100。
// 参    数：tank_offset_hpa_x100 输出鱼缸空气零点偏移，单位 hPa_x100。
// 返 回 值：1 表示存在有效打包校准；0 表示未校准或旧版记录，输出偏移归零。
// 注意事项：断电重启后由 app_state 调用，并把结果传给 WaterDepth_SetZeroOffsets。
uint8_t WaterDepth_UnpackZeroOffsets(int32_t packed,
                                     int32_t *basket_offset_hpa_x100,
                                     int32_t *tank_offset_hpa_x100);

// 函    数：WaterDepth_ConvertBasketDiffToMmX10
// 参    数：diff_hpa_x100 实时框篮传感器与空气参考的差压，单位 hPa_x100。
// 返 回 值：扣除框篮空气零点后的水深，单位 mm_x10。
// 注意事项：用于框篮控制水深，维护校准后空气中应接近 0.0mm。
int32_t WaterDepth_ConvertBasketDiffToMmX10(int32_t diff_hpa_x100);

// 函    数：WaterDepth_ConvertTankDiffToMmX10
// 参    数：diff_hpa_x100 实时鱼缸传感器与空气参考的差压，单位 hPa_x100。
// 返 回 值：扣除鱼缸空气零点后的水深，单位 mm_x10。
// 注意事项：用于鱼缸整体水深安全判断，维护校准后空气中应接近 0.0mm。
int32_t WaterDepth_ConvertTankDiffToMmX10(int32_t diff_hpa_x100);

#endif
