#ifndef __LIMIT_H
#define __LIMIT_H

#include "stm32f10x.h"

typedef enum
{
    LIMIT_LEFT_UPPER = 0,
    LIMIT_LEFT_LOWER,
    LIMIT_RIGHT_UPPER,
    LIMIT_RIGHT_LOWER,
    LIMIT_COUNT
} Limit_Channel_t;

typedef enum
{
    LIMIT_DIRECTION_UP = 0,
    LIMIT_DIRECTION_DOWN
} Limit_Direction_t;

typedef struct
{
    uint8_t left_upper;
    uint8_t left_lower;
    uint8_t right_upper;
    uint8_t right_lower;
} Limit_State_t;

void Limit_Init(void);
void Limit_Update(uint32_t now_ms);
uint8_t Limit_ReadRaw(Limit_Channel_t channel);
uint8_t Limit_IsActive(Limit_Channel_t channel);
void Limit_GetState(Limit_State_t *state);
uint8_t Limit_IsAnyUpperActive(void);
uint8_t Limit_IsAnyLowerActive(void);
uint8_t Limit_IsUpperMismatch(void);
uint8_t Limit_IsLowerMismatch(void);
uint8_t Limit_IsSameDirectionMismatch(void);
uint8_t Limit_IsDirectionBlocked(Limit_Direction_t direction);
uint8_t Limit_IsRawDirectionActive(Limit_Direction_t direction);

#endif
