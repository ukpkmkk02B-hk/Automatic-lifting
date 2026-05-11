#include "limit.h"
#include "board_config.h"

typedef struct
{
    GPIO_TypeDef *gpio;
    uint16_t pin;
} Limit_PinConfig_t;

static const Limit_PinConfig_t s_limit_pins[LIMIT_COUNT] =
{
    {BOARD_LIMIT_LEFT_UPPER_GPIO, BOARD_LIMIT_LEFT_UPPER_PIN},
    {BOARD_LIMIT_LEFT_LOWER_GPIO, BOARD_LIMIT_LEFT_LOWER_PIN},
    {BOARD_LIMIT_RIGHT_UPPER_GPIO, BOARD_LIMIT_RIGHT_UPPER_PIN},
    {BOARD_LIMIT_RIGHT_LOWER_GPIO, BOARD_LIMIT_RIGHT_LOWER_PIN}
};

static uint8_t s_raw_state[LIMIT_COUNT];
static uint8_t s_filtered_state[LIMIT_COUNT];
static uint8_t s_pending_state[LIMIT_COUNT];
static uint32_t s_pending_since_ms[LIMIT_COUNT];
static uint32_t s_last_sample_ms;
static uint8_t s_initialized;

static uint8_t Limit_TimeElapsed(uint32_t now_ms, uint32_t then_ms, uint32_t interval_ms)
{
    return ((uint32_t)(now_ms - then_ms) >= interval_ms) ? 1U : 0U;
}

static uint8_t Limit_ReadRawPin(Limit_Channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)LIMIT_COUNT)
    {
        return 1U;
    }

    return (GPIO_ReadInputDataBit(s_limit_pins[channel].gpio,
                                  s_limit_pins[channel].pin) == BOARD_LIMIT_ACTIVE_LEVEL) ? 1U : 0U;
}

void Limit_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    uint8_t i;

    RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOB, ENABLE);

    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Pin = BOARD_LIMIT_LEFT_UPPER_PIN |
                         BOARD_LIMIT_LEFT_LOWER_PIN |
                         BOARD_LIMIT_RIGHT_UPPER_PIN |
                         BOARD_LIMIT_RIGHT_LOWER_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio_init);

    for (i = 0U; i < (uint8_t)LIMIT_COUNT; i++)
    {
        s_raw_state[i] = Limit_ReadRawPin((Limit_Channel_t)i);
        s_filtered_state[i] = s_raw_state[i];
        s_pending_state[i] = s_raw_state[i];
        s_pending_since_ms[i] = 0U;
    }

    s_last_sample_ms = 0U;
    s_initialized = 1U;
}

void Limit_Update(uint32_t now_ms)
{
    uint8_t i;
    uint8_t raw;
    uint32_t confirm_ms;

    if (s_initialized == 0U)
    {
        return;
    }

    if (Limit_TimeElapsed(now_ms, s_last_sample_ms, BOARD_LIMIT_SAMPLE_MS) == 0U)
    {
        return;
    }
    s_last_sample_ms = now_ms;

    for (i = 0U; i < (uint8_t)LIMIT_COUNT; i++)
    {
        raw = Limit_ReadRawPin((Limit_Channel_t)i);
        s_raw_state[i] = raw;

        if (raw == s_filtered_state[i])
        {
            s_pending_state[i] = raw;
            s_pending_since_ms[i] = now_ms;
            continue;
        }

        if (raw != s_pending_state[i])
        {
            s_pending_state[i] = raw;
            s_pending_since_ms[i] = now_ms;
            continue;
        }

        confirm_ms = (raw != 0U) ? BOARD_LIMIT_TRIGGER_CONFIRM_MS : BOARD_LIMIT_RELEASE_CONFIRM_MS;
        if (Limit_TimeElapsed(now_ms, s_pending_since_ms[i], confirm_ms) != 0U)
        {
            s_filtered_state[i] = raw;
        }
    }
}

uint8_t Limit_ReadRaw(Limit_Channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)LIMIT_COUNT)
    {
        return 1U;
    }

    return Limit_ReadRawPin(channel);
}

uint8_t Limit_IsActive(Limit_Channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)LIMIT_COUNT)
    {
        return 1U;
    }

    return s_filtered_state[channel];
}

void Limit_GetState(Limit_State_t *state)
{
    if (state == 0)
    {
        return;
    }

    state->left_upper = s_filtered_state[LIMIT_LEFT_UPPER];
    state->left_lower = s_filtered_state[LIMIT_LEFT_LOWER];
    state->right_upper = s_filtered_state[LIMIT_RIGHT_UPPER];
    state->right_lower = s_filtered_state[LIMIT_RIGHT_LOWER];
}

uint8_t Limit_IsAnyUpperActive(void)
{
    return (uint8_t)((s_filtered_state[LIMIT_LEFT_UPPER] != 0U) ||
                     (s_filtered_state[LIMIT_RIGHT_UPPER] != 0U));
}

uint8_t Limit_IsAnyLowerActive(void)
{
    return (uint8_t)((s_filtered_state[LIMIT_LEFT_LOWER] != 0U) ||
                     (s_filtered_state[LIMIT_RIGHT_LOWER] != 0U));
}

uint8_t Limit_IsUpperMismatch(void)
{
    return (s_filtered_state[LIMIT_LEFT_UPPER] != s_filtered_state[LIMIT_RIGHT_UPPER]) ? 1U : 0U;
}

uint8_t Limit_IsLowerMismatch(void)
{
    return (s_filtered_state[LIMIT_LEFT_LOWER] != s_filtered_state[LIMIT_RIGHT_LOWER]) ? 1U : 0U;
}

uint8_t Limit_IsSameDirectionMismatch(void)
{
    return (uint8_t)((Limit_IsUpperMismatch() != 0U) || (Limit_IsLowerMismatch() != 0U));
}

uint8_t Limit_IsDirectionBlocked(Limit_Direction_t direction)
{
    if (direction == LIMIT_DIRECTION_UP)
    {
        return Limit_IsAnyUpperActive();
    }

    return Limit_IsAnyLowerActive();
}

uint8_t Limit_IsRawDirectionActive(Limit_Direction_t direction)
{
    if (direction == LIMIT_DIRECTION_UP)
    {
        return (uint8_t)((Limit_ReadRaw(LIMIT_LEFT_UPPER) != 0U) ||
                         (Limit_ReadRaw(LIMIT_RIGHT_UPPER) != 0U));
    }

    return (uint8_t)((Limit_ReadRaw(LIMIT_LEFT_LOWER) != 0U) ||
                     (Limit_ReadRaw(LIMIT_RIGHT_LOWER) != 0U));
}
