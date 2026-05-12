#ifndef __WATER_DEPTH_H
#define __WATER_DEPTH_H

#include "stm32f10x.h"
#include "error_code.h"

typedef enum
{
	WATER_DEPTH_OK = 0,
	WATER_DEPTH_PENDING,
	WATER_DEPTH_ERROR_PARAM
} WaterDepth_Status_t;

typedef struct
{
	int32_t air_pressure_hpa_x100;
	int32_t basket_pressure_hpa_x100;
	int32_t tank_pressure_hpa_x100;
	int32_t basket_depth_mm_x10;
	int32_t tank_depth_mm_x10;
	uint8_t valid;
	uint32_t timestamp_ms;
} WaterDepth_State_t;

void WaterDepth_Init(void);
void WaterDepth_Update(uint32_t now_ms);
WaterDepth_Status_t WaterDepth_GetState(WaterDepth_State_t *state);
int32_t WaterDepth_ConvertPressureDiffToMmX10(int32_t diff_hpa_x100);

#endif
