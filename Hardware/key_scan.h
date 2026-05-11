#ifndef __KEY_SCAN_H
#define __KEY_SCAN_H

#include "stm32f10x.h"

typedef enum
{
    KEY_SCAN_KEY1 = 0,
    KEY_SCAN_KEY2,
    KEY_SCAN_PAUSE_CONFIRM,
    KEY_SCAN_PAGE_MENU,
    KEY_SCAN_COUNT
} KeyScan_Key_t;

#define KEY_SCAN_EVENT_KEY1_SHORT        0x0001U
#define KEY_SCAN_EVENT_KEY2_SHORT        0x0002U
#define KEY_SCAN_EVENT_PAUSE_SHORT       0x0004U
#define KEY_SCAN_EVENT_PAGE_SHORT        0x0008U
#define KEY_SCAN_EVENT_KEY1_LONG         0x0010U
#define KEY_SCAN_EVENT_KEY2_LONG         0x0020U
#define KEY_SCAN_EVENT_PAUSE_LONG        0x0040U
#define KEY_SCAN_EVENT_PAGE_LONG         0x0080U
#define KEY_SCAN_EVENT_MAINTENANCE_ENTRY 0x0100U

void KeyScan_Init(void);
void KeyScan_Update(uint32_t now_ms);
uint16_t KeyScan_GetEvents(void);
uint8_t KeyScan_IsPressed(KeyScan_Key_t key);

#endif
