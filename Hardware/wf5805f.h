#ifndef __WF5805F_H
#define __WF5805F_H

#include "stm32f10x.h"

typedef enum
{
	WF5805F_SENSOR_AIR = 0,
	WF5805F_SENSOR_BASKET,
	WF5805F_SENSOR_TANK,
	WF5805F_SENSOR_COUNT
} WF5805F_Sensor_t;

typedef enum
{
	WF5805F_OK = 0,
	WF5805F_PENDING,
	WF5805F_ERROR_PARAM,
	WF5805F_ERROR_I2C,
	WF5805F_ERROR_TIMEOUT,
	WF5805F_ERROR_STATUS
} WF5805F_Status_t;

typedef struct
{
	int32_t pressure_hpa_x100;
	int16_t temperature_c_x100;
	uint8_t raw[5];
	uint8_t status;
	uint8_t valid;
	uint32_t timestamp_ms;
} WF5805F_Reading_t;

void WF5805F_InitAll(void);
void WF5805F_Update(uint32_t now_ms);
WF5805F_Status_t WF5805F_GetReading(WF5805F_Sensor_t sensor, WF5805F_Reading_t *reading);
uint16_t WF5805F_GetFailureCount(WF5805F_Sensor_t sensor);
uint16_t WF5805F_GetRecoveryFailureCount(WF5805F_Sensor_t sensor);
WF5805F_Status_t WF5805F_ResetBus(WF5805F_Sensor_t sensor);

#endif
