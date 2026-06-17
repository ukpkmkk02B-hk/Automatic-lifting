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

// 函    数：Limit_TimeElapsed
// 参    数：now_ms 当前毫秒时间戳；then_ms 起始时间戳；interval_ms 目标间隔。
// 返 回 值：达到或超过间隔返回 1，否则返回 0。
// 注意事项：使用无符号差值，允许 now_ms 溢出回绕后继续工作。
static uint8_t Limit_TimeElapsed(uint32_t now_ms, uint32_t then_ms, uint32_t interval_ms)
{
    return ((uint32_t)(now_ms - then_ms) >= interval_ms) ? 1U : 0U;
}

// 函    数：Limit_ReadRawPin
// 参    数：channel 限位通道，范围为 Limit_Channel_t。
// 返 回 值：1 表示原始 GPIO 已触发，0 表示未触发；非法通道返回 1。
// 注意事项：光耦输出低电平表示触发，软件统一转换为 1=触发。
static uint8_t Limit_ReadRawPin(Limit_Channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)LIMIT_COUNT)
    {
        // 无效通道按已触发处理，避免参数错误导致运动绕过限位保护。
        return 1U;
    }

    // BOARD_LIMIT_ACTIVE_LEVEL 为 Bit_RESET，对应 24V NPN 限位经光耦后的低有效信号。
    return (GPIO_ReadInputDataBit(s_limit_pins[channel].gpio,
                                  s_limit_pins[channel].pin) == BOARD_LIMIT_ACTIVE_LEVEL) ? 1U : 0U;
}

// 函    数：Limit_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 PB12-PB15 为上拉输入；上电当前物理状态直接作为滤波初值。
void Limit_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    uint8_t i;

    RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOB, ENABLE);

    // 限位模块 MCU 侧使用上拉输入，未触发时应读高电平。
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Pin = BOARD_LIMIT_LEFT_UPPER_PIN |
                         BOARD_LIMIT_LEFT_LOWER_PIN |
                         BOARD_LIMIT_RIGHT_UPPER_PIN |
                         BOARD_LIMIT_RIGHT_LOWER_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio_init);

    for (i = 0U; i < (uint8_t)LIMIT_COUNT; i++)
    {
        // 初始化时把当前物理状态作为稳定状态，避免上电后短时间误判跳变。
        s_raw_state[i] = Limit_ReadRawPin((Limit_Channel_t)i);
        s_filtered_state[i] = s_raw_state[i];
        s_pending_state[i] = s_raw_state[i];
        s_pending_since_ms[i] = 0U;
    }

    s_last_sample_ms = 0U;
    s_initialized = 1U;
}

// 函    数：Limit_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：按采样周期推进滤波；触发确认快、释放确认慢，兼顾急停和抗抖动。
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
        // 限位滤波按固定采样周期推进，主循环频率变化不改变滤波时间常数。
        return;
    }
    s_last_sample_ms = now_ms;

    for (i = 0U; i < (uint8_t)LIMIT_COUNT; i++)
    {
        raw = Limit_ReadRawPin((Limit_Channel_t)i);
        s_raw_state[i] = raw;

        if (raw == s_filtered_state[i])
        {
            // 原始值与稳定值一致时刷新候选状态，后续跳变必须重新计时。
            s_pending_state[i] = raw;
            s_pending_since_ms[i] = now_ms;
            continue;
        }

        if (raw != s_pending_state[i])
        {
            // 新候选电平第一次出现，开始按触发/释放确认时间计时。
            s_pending_state[i] = raw;
            s_pending_since_ms[i] = now_ms;
            continue;
        }

        // 触发确认 20ms、释放确认 50ms；触发更快是为了运动方向限位尽早生效。
        confirm_ms = (raw != 0U) ? BOARD_LIMIT_TRIGGER_CONFIRM_MS : BOARD_LIMIT_RELEASE_CONFIRM_MS;
        if (Limit_TimeElapsed(now_ms, s_pending_since_ms[i], confirm_ms) != 0U)
        {
            s_filtered_state[i] = raw;
        }
    }
}

// 函    数：Limit_ReadRaw
// 参    数：channel 限位通道，范围为 Limit_Channel_t。
// 返 回 值：1 表示原始 GPIO 已触发，0 表示未触发；非法通道返回 1。
// 注意事项：该接口供 STEP 中断急停使用，不经过软件滤波。
uint8_t Limit_ReadRaw(Limit_Channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)LIMIT_COUNT)
    {
        return 1U;
    }

    return Limit_ReadRawPin(channel);
}

// 函    数：Limit_IsActive
// 参    数：channel 限位通道，范围为 Limit_Channel_t。
// 返 回 值：1 表示滤波后已触发，0 表示未触发；非法通道返回 1。
// 注意事项：用于主循环显示和启动前判断，不用于中断急停。
uint8_t Limit_IsActive(Limit_Channel_t channel)
{
    if ((uint32_t)channel >= (uint32_t)LIMIT_COUNT)
    {
        return 1U;
    }

    return s_filtered_state[channel];
}

// 函    数：Limit_GetState
// 参    数：state 输出四路限位状态的结构体指针，允许为 NULL。
// 返 回 值：无
// 注意事项：输出的是滤波后的稳定状态，字段值 1=触发、0=未触发。
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

// 函    数：Limit_IsAnyUpperActive
// 参    数：无
// 返 回 值：任意上限位滤波后触发返回 1，否则返回 0。
// 注意事项：返回 1 时必须禁止上升方向运动。
uint8_t Limit_IsAnyUpperActive(void)
{
    return (uint8_t)((s_filtered_state[LIMIT_LEFT_UPPER] != 0U) ||
                     (s_filtered_state[LIMIT_RIGHT_UPPER] != 0U));
}

// 函    数：Limit_IsAnyLowerActive
// 参    数：无
// 返 回 值：任意下限位滤波后触发返回 1，否则返回 0。
// 注意事项：返回 1 时必须禁止下降方向运动。
uint8_t Limit_IsAnyLowerActive(void)
{
    return (uint8_t)((s_filtered_state[LIMIT_LEFT_LOWER] != 0U) ||
                     (s_filtered_state[LIMIT_RIGHT_LOWER] != 0U));
}

// 函    数：Limit_IsUpperMismatch
// 参    数：无
// 返 回 值：左右上限位滤波状态不一致返回 1，否则返回 0。
// 注意事项：不一致通常表示两侧机械不同步、限位损坏或接线异常。
uint8_t Limit_IsUpperMismatch(void)
{
    return (s_filtered_state[LIMIT_LEFT_UPPER] != s_filtered_state[LIMIT_RIGHT_UPPER]) ? 1U : 0U;
}

// 函    数：Limit_IsLowerMismatch
// 参    数：无
// 返 回 值：左右下限位滤波状态不一致返回 1，否则返回 0。
// 注意事项：不一致通常表示两侧机械不同步、限位损坏或接线异常。
uint8_t Limit_IsLowerMismatch(void)
{
    return (s_filtered_state[LIMIT_LEFT_LOWER] != s_filtered_state[LIMIT_RIGHT_LOWER]) ? 1U : 0U;
}

// 函    数：Limit_IsSameDirectionMismatch
// 参    数：无
// 返 回 值：任一同方向左右限位不一致返回 1，否则返回 0。
// 注意事项：返回 1 时应停止全部运动，而不是只禁止某一个方向。
uint8_t Limit_IsSameDirectionMismatch(void)
{
    return (uint8_t)((Limit_IsUpperMismatch() != 0U) || (Limit_IsLowerMismatch() != 0U));
}

// 函    数：Limit_IsDirectionBlocked
// 参    数：direction 运动方向，上升检查上限位，下降检查下限位。
// 返 回 值：1 表示该方向被滤波限位禁止，0 表示未禁止。
// 注意事项：用于启动运动前的安全检查。
uint8_t Limit_IsDirectionBlocked(Limit_Direction_t direction)
{
    if (direction == LIMIT_DIRECTION_UP)
    {
        // 上升时任意上限位触发都禁止继续上升。
        return Limit_IsAnyUpperActive();
    }

    // 下降时任意下限位触发都禁止继续下降。
    return Limit_IsAnyLowerActive();
}

// 函    数：Limit_IsRawDirectionActive
// 参    数：direction 运动方向，上升直读上限位，下降直读下限位。
// 返 回 值：1 表示当前方向原始限位已触发，0 表示未触发。
// 注意事项：用于 TIM2 STEP 中断内急停，不等待 BOARD_LIMIT_TRIGGER_CONFIRM_MS。
uint8_t Limit_IsRawDirectionActive(Limit_Direction_t direction)
{
    if (direction == LIMIT_DIRECTION_UP)
    {
        // 中断急停路径使用原始 GPIO，不等待滤波确认。
        return (uint8_t)((Limit_ReadRaw(LIMIT_LEFT_UPPER) != 0U) ||
                         (Limit_ReadRaw(LIMIT_RIGHT_UPPER) != 0U));
    }

    return (uint8_t)((Limit_ReadRaw(LIMIT_LEFT_LOWER) != 0U) ||
                     (Limit_ReadRaw(LIMIT_RIGHT_LOWER) != 0U));
}
