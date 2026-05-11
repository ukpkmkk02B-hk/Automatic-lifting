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

static uint8_t KeyScan_TimeElapsed(uint32_t now_ms, uint32_t then_ms, uint32_t interval_ms)
{
    return ((uint32_t)(now_ms - then_ms) >= interval_ms) ? 1U : 0U;
}

static uint8_t KeyScan_ReadRawPin(KeyScan_Key_t key)
{
    if ((uint32_t)key >= (uint32_t)KEY_SCAN_COUNT)
    {
        return 0U;
    }

    return (GPIO_ReadInputDataBit(s_key_pins[key].gpio,
                                  s_key_pins[key].pin) == BOARD_KEY_ACTIVE_LEVEL) ? 1U : 0U;
}

static void KeyScan_HandleStableChange(KeyScan_Key_t key, uint32_t now_ms, uint8_t pressed)
{
    uint32_t held_ms;

    s_key_state[key].stable_pressed = pressed;

    if (pressed != 0U)
    {
        s_key_state[key].pressed_since_ms = now_ms;
        s_key_state[key].maintenance_reported = 0U;
        return;
    }

    held_ms = (uint32_t)(now_ms - s_key_state[key].pressed_since_ms);
    if (s_key_state[key].maintenance_reported != 0U)
    {
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

void KeyScan_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    uint8_t i;

    RCC_APB2PeriphClockCmd(BOARD_RCC_GPIOB, ENABLE);

    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Pin = BOARD_KEY1_PIN |
                         BOARD_KEY2_PIN |
                         BOARD_KEY_PAUSE_PIN |
                         BOARD_KEY_PAGE_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio_init);

    for (i = 0U; i < (uint8_t)KEY_SCAN_COUNT; i++)
    {
        s_key_state[i].raw_pressed = KeyScan_ReadRawPin((KeyScan_Key_t)i);
        s_key_state[i].stable_pressed = s_key_state[i].raw_pressed;
        s_key_state[i].pending_pressed = s_key_state[i].raw_pressed;
        s_key_state[i].maintenance_reported = 0U;
        s_key_state[i].pending_since_ms = 0U;
        s_key_state[i].pressed_since_ms = 0U;
    }

    s_last_scan_ms = 0U;
    s_events = 0U;
    s_initialized = 1U;
}

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
        return;
    }
    s_last_scan_ms = now_ms;

    for (i = 0U; i < (uint8_t)KEY_SCAN_COUNT; i++)
    {
        raw = KeyScan_ReadRawPin((KeyScan_Key_t)i);
        s_key_state[i].raw_pressed = raw;

        if (raw == s_key_state[i].stable_pressed)
        {
            s_key_state[i].pending_pressed = raw;
            s_key_state[i].pending_since_ms = now_ms;
        }
        else
        {
            if (raw != s_key_state[i].pending_pressed)
            {
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

        if ((i == (uint8_t)KEY_SCAN_PAUSE_CONFIRM) &&
            (s_key_state[i].stable_pressed != 0U) &&
            (s_key_state[i].maintenance_reported == 0U))
        {
            held_ms = (uint32_t)(now_ms - s_key_state[i].pressed_since_ms);
            if (held_ms >= BOARD_KEY_MAINTENANCE_MS)
            {
                s_events |= KEY_SCAN_EVENT_MAINTENANCE_ENTRY;
                s_key_state[i].maintenance_reported = 1U;
            }
        }
    }
}

uint16_t KeyScan_GetEvents(void)
{
    uint16_t events;

    events = s_events;
    s_events = 0U;
    return events;
}

uint8_t KeyScan_IsPressed(KeyScan_Key_t key)
{
    if ((uint32_t)key >= (uint32_t)KEY_SCAN_COUNT)
    {
        return 0U;
    }

    return s_key_state[key].stable_pressed;
}
