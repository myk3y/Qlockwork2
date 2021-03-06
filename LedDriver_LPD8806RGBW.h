/******************************************************************************
LedDriver_LPD8806RGBW.h
******************************************************************************/

#ifndef LEDDRIVER_LPD8806RGBW_H
#define LEDDRIVER_LPD8806RGBW_H

#include "LPD8806RGBW.h"
#include "Colors.h"
#include "Configuration.h"

#define NUM_LEDS 115

class LedDriver_LPD8806RGBW
{
public:
	LedDriver_LPD8806RGBW();
	String getSignature();
	void clear();
	void show();
	void setPixel(uint8_t x, uint8_t y, uint8_t color, uint8_t brightness);
	void setPixel(uint8_t num, uint8_t color, uint8_t brightness);

private:
	LPD8806RGBW *strip;
};

#endif
