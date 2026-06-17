#ifndef __CRC16_H
#define __CRC16_H

#include "stm32f10x.h"

// 模    块：CRC16 校验
// 用    途：为 Flash 参数记录提供断电恢复校验，避免使用写坏或半写入的数据。
// 算法约定：CRC-16/CCITT-FALSE，多项式 0x1021，初值 0xFFFF，结果不取反。

// 函    数：CRC16_CcittFalse
// 参    数：data 待校验数据首地址；length 数据长度，单位 byte。
// 返 回 值：CRC16 校验值。
// 注意事项：data 为 NULL 且 length 非 0 时返回初值 0xFFFF；本函数不分配动态内存。
uint16_t CRC16_CcittFalse(const uint8_t *data, uint16_t length);

#endif
