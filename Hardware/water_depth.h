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

// 函    数：WaterDepth_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 5 点压力滤波器和水深状态，初始状态为未有效。
void WaterDepth_Init(void);

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

// 函    数：WaterDepth_ConvertPressureDiffToMmX10
// 参    数：diff_hpa_x100 压力差，单位 hPa_x100。
// 返 回 值：水深，单位 mm_x10。
// 注意事项：按 1hPa≈10.197mmH2O 整数换算，避免使用浮点。
int32_t WaterDepth_ConvertPressureDiffToMmX10(int32_t diff_hpa_x100);

#endif
