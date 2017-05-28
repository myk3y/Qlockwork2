/******************************************************************************
   LedDriver_FastLED.cpp
******************************************************************************/

#include "LedDriver_FastLED.h"

LedDriver_FastLED::LedDriver_FastLED() {
#ifdef LED_DRIVER_FAST_APA102
	FastLED.addLeds<APA102, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_APA104
	FastLED.addLeds<APA104, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_APA106
	FastLED.addLeds<APA106, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_DOTSTAR
	FastLED.addLeds<DOTSTAR, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_DMXSIMPLE
	FastLED.addLeds<DMXSIMPLE, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_GW6205
	FastLED.addLeds<GW6205, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_GW6205_400
	FastLED.addLeds<GW6205_400, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_LPD1886
	FastLED.addLeds<LPD1886, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_LPD1886_8BIT
	FastLED.addLeds<LPD1886_8BIT, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_LPD8806
	FastLED.addLeds<LPD8806, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_NEOPIXEL
	FastLED.addLeds<NEOPIXEL, PIN_LEDS_DATA>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_P9813
	FastLED.addLeds<P9813, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_PL9823
	FastLED.addLeds<PL9823, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_SK6812
	FastLED.addLeds<SK6812, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_SK6822
	FastLED.addLeds<SK6822, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_SK9822
	FastLED.addLeds<SK9822, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_SM16716
	FastLED.addLeds<SM16716, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_TM1803
	FastLED.addLeds<TM1803, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_TM1804
	FastLED.addLeds<TM1804, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_TM1809
	FastLED.addLeds<TM1809, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_TM1812
	FastLED.addLeds<TM1812, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_TM1829
	FastLED.addLeds<TM1829, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_UCS1903
	FastLED.addLeds<UCS1903, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_UCS1903B
	FastLED.addLeds<UCS1903B, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_UCS1904
	FastLED.addLeds<UCS1904, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_UCS2903
	FastLED.addLeds<UCS2903, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2801
	FastLED.addLeds<WS2801, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2803
	FastLED.addLeds<WS2803, PIN_LEDS_DATA, PIN_LEDS_CLOCK, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2811
	FastLED.addLeds<WS2811, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2811_400
	FastLED.addLeds<WS2811_400, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2812
	FastLED.addLeds<WS2812, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2812B
	FastLED.addLeds<WS2812B, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2813
	FastLED.addLeds<WS2813, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
#ifdef LED_DRIVER_FAST_WS2852
	FastLED.addLeds<WS2852, PIN_LEDS_DATA, RGB>(leds, NUM_LEDS);
#endif
}

String LedDriver_FastLED::getSignature() {
	return "FastLED";
}

void LedDriver_FastLED::writeScreenBufferToLEDs(word matrix[16], uint8_t color, uint8_t brightness) {
	FastLED.clear();
	for (uint8_t y = 0; y < 10; y++) {
		for (uint8_t x = 5; x < 16; x++) {
			uint16_t t = 1 << x;
			if ((matrix[y] & t) == t) setPixel(x, y, color, brightness);
		}
	}
	// set corners and alarm
	for (uint8_t i = 0; i < 5; i++) if ((matrix[i] & 0b0000000000010000) > 0) {
#ifdef LED_LAYOUT_HORIZONTAL
		setPixel(i * -1 + 15, 10, color, brightness);
#endif // LED_LAYOUT_HORIZONTAL
#ifdef LED_LAYOUT_VERTICAL
		setPixel(5, i + 10, color, brightness);
#endif // LED_LAYOUT_VERTICAL
	}
	FastLED.show();
}

void LedDriver_FastLED::setPixel(uint8_t x, uint8_t y, uint8_t color, uint8_t brightness) {
	uint8_t red = map(brightness, 0, 255, 0, defaultColors[color].red);
	uint8_t green = map(brightness, 0, 255, 0, defaultColors[color].green);
	uint8_t blue = map(brightness, 0, 255, 0, defaultColors[color].blue);
	uint32_t ledColor = (red << 16) + (green << 8) + blue;
#ifdef LED_LAYOUT_HORIZONTAL
	uint8_t num = 15 - x + y * 11;
	if (num < 110) {
		if (num / 11 % 2 == 0) leds[num] = ledColor;
		else leds[num / 11 * 11 + 10 - (num % 11)] = ledColor;
	}
	else {
		switch (num) {
		case 110: // upper-left
			leds[111] = ledColor;
			break;
		case 111: // upper-right
			leds[112] = ledColor;
			break;
		case 112: // bottom-right
			leds[113] = ledColor;
			break;
		case 113: // bottom-left
			leds[110] = ledColor;
			break;
		case 114: // alarm
			leds[114] = ledColor;
			break;
		default:
			break;
		}
	}
#endif // LED_LAYOUT_HORIZONTAL
#ifdef LED_LAYOUT_VERTICAL
	uint8_t num = y + (15 - x) * 10;
	uint8_t ledNum;
	if (num < 110) {
		if (num / 10 % 2 == 0) ledNum = num;
		else ledNum = num / 10 * 10 + 9 - (num % 10);
		if (ledNum < 10) leds[ledNum + 1] = ledColor;
		else if (ledNum < 100) leds[ledNum + 2] = ledColor;
		else leds[ledNum + 3] = ledColor;
	}
	else {
		switch (num) {
		case 110: // upper-left
			leds[0] = ledColor;
			break;
		case 111: // upper-right
			leds[102] = ledColor;
			break;
		case 112: // bottom-right
			leds[113] = ledColor;
			break;
		case 113: // bottom-left
			leds[11] = ledColor;
			break;
		case 114: // alarm
			leds[114] = ledColor;
			break;
		default:
			break;
		}
	}
#endif // LED_LAYOUT_VERTICAL
}