#ifndef __OLED_H
#define __OLED_H

// 模    块：0.96 寸 I2C OLED 基础显示驱动
// 职    责：提供 4 行 x 16 字符的定位显示接口，供 ui_pages/menu 输出 ASCII 页面。
// 硬件假设：OLED 总线和引脚初始化由 OLED.c 内部完成；本驱动为阻塞式软件 I2C 写屏，禁止在中断中调用。

// 函    数：OLED_Init
// 参    数：无
// 返 回 值：无
// 注意事项：上电后调用一次，完成 GPIO/软件 I2C 和 SSD1306 初始化序列。
void OLED_Init(void);

// 函    数：OLED_Clear
// 参    数：无
// 返 回 值：无
// 注意事项：清空整屏显存；阻塞时间由 OLED I2C 写屏耗时决定。
void OLED_Clear(void);

// 函    数：OLED_ShowChar
// 参    数：Line 行号，范围 1-4；Column 列号，范围 1-16；Char 待显示 ASCII 字符。
// 返 回 值：无
// 注意事项：超过屏幕范围的坐标会导致底层写入越界显示区域，调用方应先限制位置。
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);

// 函    数：OLED_ShowString
// 参    数：Line 行号，范围 1-4；Column 起始列，范围 1-16；String 以 '\0' 结尾的 ASCII 字符串。
// 返 回 值：无
// 注意事项：不会自动换行；调用方负责保证字符串长度适合 16 字符行宽。
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);

// 函    数：OLED_ShowNum
// 参    数：Line 行号；Column 起始列；Number 无符号数值；Length 固定显示位数。
// 返 回 值：无
// 注意事项：不足位补前导 0，主要用于固定宽度状态页面。
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

// 函    数：OLED_ShowSignedNum
// 参    数：Line 行号；Column 起始列；Number 有符号数值；Length 绝对值显示位数。
// 返 回 值：无
// 注意事项：始终显示符号位，适合位置、水深误差等有正负含义的数值。
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);

// 函    数：OLED_ShowHexNum
// 参    数：Line 行号；Column 起始列；Number 数值；Length 十六进制固定显示位数。
// 返 回 值：无
// 注意事项：用于调试寄存器、错误码等固定宽度十六进制显示。
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

// 函    数：OLED_ShowBinNum
// 参    数：Line 行号；Column 起始列；Number 数值；Length 二进制固定显示位数。
// 返 回 值：无
// 注意事项：用于调试按键、限位等位状态，显示长度不要超过当前行剩余列数。
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif
