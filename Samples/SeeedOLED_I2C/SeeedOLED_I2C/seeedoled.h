#pragma once
#pragma once
//GROVE_NAME        "Grove - OLED Display 1.12'' V2"
//SKU               101020132
//WIKI_URL          http://wiki.seeedstudio.com/Grove-OLED_Display_1.12inch/
#pragma once

#include "applibs_versions.h"
#include <applibs/gpio.h>

#define SH1107G  1
#define SSD1327  2

#define VERTICAL_MODE                       01
#define HORIZONTAL_MODE                     02

#define SeeedGrayOLED_Address		0x3c


/*Command and register */
#define SeeedGrayOLED_Command_Mode          0x80
#define SeeedGrayOLED_Data_Mode				 0x40

#define SeeedGrayOLED_Display_Off_Cmd       0xAE
#define SeeedGrayOLED_Display_On_Cmd        0xAF

#define SeeedGrayOLED_Normal_Display_Cmd    0xA4
#define SeeedGrayOLED_Inverse_Display_Cmd   0xA7
#define SeeedGrayOLED_Activate_Scroll_Cmd   0x2F
#define SeeedGrayOLED_Dectivate_Scroll_Cmd  0x2E
#define SeeedGrayOLED_Set_ContrastLevel_Cmd  0x81

#define Scroll_Left             0x00
#define Scroll_Right            0x01

#define Scroll_2Frames          0x7
#define Scroll_3Frames          0x4
#define Scroll_4Frames          0x5
#define Scroll_5Frames          0x0
#define Scroll_25Frames         0x6
#define Scroll_64Frames         0x1
#define Scroll_128Frames        0x2
#define Scroll_256Frames        0x3

unsigned int GroveOled_GetIC(void);
void GroveOledDisplay_Init(int i2cFd, uint8_t IC);

void setNormalDisplay(void);
void setInverseDisplay(void);

void setGrayLevel(unsigned char grayLevel);

void setVerticalMode(void);
void setHorizontalMode(void);

void setTextXY(unsigned char Row, unsigned char Column);
void clearDisplay(void);
void setContrastLevel(unsigned char ContrastLevel);
void putChar(unsigned char c);
void putString(const char *String);
unsigned char putNumber(long n);

void drawBitmap(const unsigned char *bitmaparray, int bytes);

void setHorizontalScrollProperties(bool direction, unsigned char startRow, unsigned char endRow, unsigned char startColumn, unsigned char endColumn, unsigned char scrollSpeed);
void activateScroll(void);
void deactivateScroll(void);