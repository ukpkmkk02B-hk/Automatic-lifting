#include "key_scan.h"
#include "board_config.h"

typedef struct
{
    GPIO_TypeDef *gpio;
    uint16_t pin;
    uint16_t short_event;
    uint16_t long_event;
} KeyScan_PinConfig_t;

typedef struct
{
    uint8_t raw_pressed;
    uint8_t stable_pressed;
    uint8_t pending_pressed;
    uint8_t maintenance_reported;
    uint8_t long_reported;
    uint32_t pending_since_ms;
    uint32_t pressed_since_ms;
} KeyScan_State_t;

static const KeyScan_PinConfig_t s_key_pins[KEY_SCAN_COUNT] =
{
    {BOARD_KEY1_GPIO, BOARD_KEY1_PIN, KEY_SCAN_EVENT_KEY1_SHORT, KEY_SCAN_EVENT_KEY1_LONG},
    {BOARD_KEY2_GPIO, BOARD_KEY2_PIN, KEY_SCAN_EVENT_KEY2_SHORT, KEY_SCAN_EVENT_KEY2_LONG},
    {BOARD_KEY_PAUSE_GPIO, BOARD_KEY_PAUSE_PIN, KEY_SCAN_EVENT_PAUSE_SHORT, KEY_SCAN_EVENT_PAUSE_LONG},
    {BOARD_KEY_PAGE_GPIO, BOARD_KEY_PAGE_PIN, KEY_SCAN_EVENT_PAGE_SHORT, KEY_SCAN_EVENT_PAGE_LONG}
};

static KeyScan_State_t s_key_state[KEY_SCAN_COUNT];
static uint32_t s_last_scan_ms;
static uint16_t s_events;
static uint8_t s_initialized;

// 函    数：KeyScan_ReportLongWhileHeld
// 参    数：key 按键编号，范围为 KeyScan_Key_t。
// 返 回 值：1 表示普通长按需要在仍按住时上报，0 表示按释放沿上报。
// 注意事项：PB1/PB11 手动点动依赖“达到长按阈值即开始运动”，不能等到松手后才出事件。
static uint8_t KeyScan_ReportLongWhileHeld(KeyScan_Key_t key)
{
    return ((key == KEY_SCAN_KEY1) || (key == KEY_SCAN_KEY2)) ? 1U : 0U;
}

// 函    数：KeyScan_TimeElapsed
// 参    数：now_ms 当前毫秒时间戳；then_ms 起始时间戳；interval_ms 目标间隔。
// 返 回 值：达到或超过间隔返回 1，否则返回 0。
// 注意事项：使用无符号差值，允许毫秒计数回绕。
static uint8_t KeyScan_TimeElapsed(uint32_t now_ms, uint32_t then_ms, uint32_t interval_ms)
{
    return ((uint32_t)(now_ms - then_ms) >= interval_ms) ? 1U : 0U;
}

// 函    数：KeyScan_ReadRawPin
// 参    数：key 按键编号，范围为 KeyScan_Key_t。
// 返 回 值：1 表示原始 GPIO 为按下，0 表示松开；非法按键返回 0。
// 注意事项：按键低有效，读取后统一转换为 1=按下。
static uint8_t KeyScan_ReadRawPin(KeyScan_Key_t key)
{
    if ((uint32_t)key >= (uint32_t)KEY_SCAN_COUNT)
    {
        // 无效按键按未按下处理，避免产生不存在的用户事件。
        return 0U;
    }

    // BOARD_KEY_ACTIVE_LEVEL 为 Bit_RESET，对应按下时 GPIO 被拉低。
    return (GPIO_ReadInputDataBit(s_key_pins[key].gpio,
                                  s_key_pins[key].pin) == BOARD_KEY_ACTIVE_LEVEL) ? 1U : 0U;
}

// 函    数：KeyScan_HandleStableChange
// 参    数：key 按键编号；now_ms 当前毫秒时间戳；pressed 消抖后的稳定按下状态。
// 返 回 值：无
// 注意事项：按下沿记录时间并清除一次性锁存；释放沿只生成尚未上报的短按/长按事件。
static void KeyScan_HandleStableChange(KeyScan_Key_t key, uint32_t now_ms, uint8_t pressed)
{
    uint32_t held_ms;

    s_key_state[key].stable_pressed = pressed;

    if (pressed != 0U)
    {
        // 按下沿记录起始时间；PB1/PB11 普通长按会在保持按下时由 KeyScan_Update 上报一次。
        s_key_state[key].pressed_since_ms = now_ms;
        s_key_state[key].maintenance_reported = 0U;
        s_key_state[key].long_reported = 0U;
        return;
    }

    held_ms = (uint32_t)(now_ms - s_key_state[key].pressed_since_ms);
    if (s_key_state[key].maintenance_reported != 0U)
    {
        // PB10 维护入口已上报后，释放时不再额外生成普通长按事件。
        return;
    }
    if (s_key_state[key].long_reported != 0U)
    {
        // PB1/PB11 已在按住达到长按阈值时上报，释放时只负责结束保持状态。
        return;
    }

    if (held_ms >= BOARD_KEY_LONG_MS)
    {
        s_events |= s_key_pins[key].long_event;
    }
    else if (held_ms >= BOARD_KEY_SHORT_MIN_MS)
    {
        s_events |= s_key_pins[key].short_event;
    }
}

// 函    数：KeyScan_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化 PB1/PB11/PB10/PB0 为上拉输入，并以当前状态作为消抖起点。
void KeyScan_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    uint8_t i;

    RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOB, ENABLE);

    // 按键输入使用上拉，按下时被拉低。
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Pin = BOARD_KEY1_PIN |
                         BOARD_KEY2_PIN |
                         BOARD_KEY_PAUSE_PIN |
                         BOARD_KEY_PAGE_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio_init);

    for (i = 0U; i < (uint8_t)KEY_SCAN_COUNT; i++)
    {
        // 上电时不强制等待松手，只把当前状态作为消抖起点。
        s_key_state[i].raw_pressed = KeyScan_ReadRawPin((KeyScan_Key_t)i);
        s_key_state[i].stable_pressed = s_key_state[i].raw_pressed;
        s_key_state[i].pending_pressed = s_key_state[i].raw_pressed;
        s_key_state[i].maintenance_reported = 0U;
        s_key_state[i].long_reported = 0U;
        s_key_state[i].pending_since_ms = 0U;
        s_key_state[i].pressed_since_ms = 0U;
    }

    s_last_scan_ms = 0U;
    s_events = 0U;
    s_initialized = 1U;
}

// 函    数：KeyScan_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：非阻塞扫描；PB1/PB11 普通长按在按住达到阈值时上报，短按仍在释放时确认。
void KeyScan_Update(uint32_t now_ms)
{
    uint8_t i;
    uint8_t raw;
    uint32_t held_ms;

    if (s_initialized == 0U)
    {
        return;
    }

    if (KeyScan_TimeElapsed(now_ms, s_last_scan_ms, BOARD_KEY_SCAN_PERIOD_MS) == 0U)
    {
        // 扫描周期固定为 10ms，避免主循环快慢影响消抖判定。
        return;
    }
    s_last_scan_ms = now_ms;

    for (i = 0U; i < (uint8_t)KEY_SCAN_COUNT; i++)
    {
        raw = KeyScan_ReadRawPin((KeyScan_Key_t)i);
        s_key_state[i].raw_pressed = raw;

        if (raw == s_key_state[i].stable_pressed)
        {
            // 原始状态回到稳定状态时，取消正在观察的跳变。
            s_key_state[i].pending_pressed = raw;
            s_key_state[i].pending_since_ms = now_ms;
        }
        else
        {
            if (raw != s_key_state[i].pending_pressed)
            {
                // 新跳变候选出现，重新计 25ms 消抖时间。
                s_key_state[i].pending_pressed = raw;
                s_key_state[i].pending_since_ms = now_ms;
            }
            else if (KeyScan_TimeElapsed(now_ms,
                                         s_key_state[i].pending_since_ms,
                                         BOARD_KEY_DEBOUNCE_MS) != 0U)
            {
                KeyScan_HandleStableChange((KeyScan_Key_t)i, now_ms, raw);
            }
        }

        if ((s_key_state[i].stable_pressed != 0U) &&
            (s_key_state[i].long_reported == 0U) &&
            (KeyScan_ReportLongWhileHeld((KeyScan_Key_t)i) != 0U))
        {
            // 手动页需要“按住到 1000ms 后立即开始点动”；事件只上报一次，持续保持由 KeyScan_IsPressed 查询。
            held_ms = (uint32_t)(now_ms - s_key_state[i].pressed_since_ms);
            if (held_ms >= BOARD_KEY_LONG_MS)
            {
                s_events |= s_key_pins[i].long_event;
                s_key_state[i].long_reported = 1U;
            }
        }

        if ((i == (uint8_t)KEY_SCAN_PAUSE_CONFIRM) &&
            (s_key_state[i].stable_pressed != 0U) &&
            (s_key_state[i].maintenance_reported == 0U))
        {
            // PB10 长按 3 秒进入维护入口，事件只上报一次。
            held_ms = (uint32_t)(now_ms - s_key_state[i].pressed_since_ms);
            if (held_ms >= BOARD_KEY_MAINTENANCE_MS)
            {
                s_events |= KEY_SCAN_EVENT_MAINTENANCE_ENTRY;
                s_key_state[i].maintenance_reported = 1U;
            }
        }
    }
}

// 函    数：KeyScan_GetEvents
// 参    数：无
// 返 回 值：按键事件位图，读取后清空。
// 注意事项：事件为边沿型缓存，同一次短按或长按只被消费一次。
uint16_t KeyScan_GetEvents(void)
{
    uint16_t events;

    events = s_events;
    s_events = 0U;
    return events;
}

// 函    数：KeyScan_IsPressed
// 参    数：key 按键编号，范围为 KeyScan_Key_t。
// 返 回 值：1 表示滤波后按下，0 表示松开或参数非法。
// 注意事项：用于手动点动等需要查询当前按住状态的场景。
uint8_t KeyScan_IsPressed(KeyScan_Key_t key)
{
    if ((uint32_t)key >= (uint32_t)KEY_SCAN_COUNT)
    {
        return 0U;
    }

    return s_key_state[key].stable_pressed;
}
