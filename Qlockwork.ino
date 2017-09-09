/******************************************************************************
QLOCKWORK
An advanced firmware for a DIY "word-clock".

@mc      ESP8266
@created 01.02.2017
******************************************************************************/

#define FIRMWARE_VERSION 20170909

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DS3232RTC.h>
#include <DHT.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <RestClient.h>
#include <Syslog.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <WiFiManager.h>
#include "Colors.h"
#include "Configuration.h"
#include "Debug.h"
#include "Events.h"
#include "Languages.h"
#include "LedDriver_FastLED.h"
#include "LedDriver_NeoPixel.h"
#include "LedDriver_LPD8806RGBW.h"
#include "Modes.h"
#include "Renderer.h"
#include "Settings.h"
#include "Timezones.h"

/******************************************************************************
Init.
******************************************************************************/

#ifdef LED_LIBRARY_FASTLED
LedDriver_FastLED ledDriver;
#endif
#ifdef LED_LIBRARY_LPD8806RGBW
LedDriver_LPD8806RGBW ledDriver;
#endif
#ifdef LED_LIBRARY_NEOPIXEL
LedDriver_NeoPixel ledDriver;
#endif

ESP8266WebServer esp8266WebServer(80);
ESP8266HTTPUpdateServer httpUpdater;
Renderer renderer;
Settings settings;
Debug debug;

uint16_t matrix[10] = {};
uint16_t matrixOld[10] = {};
boolean screenBufferNeedsUpdate = true;
Mode mode = STD_MODE_TIME;
Mode lastMode = mode;
uint32_t modeTimeout = 0;
uint32_t titleTimeout = 0;
uint32_t autoModeChange = AUTO_MODECHANGE_TIME;
boolean runTransitionOnce = false;
uint8_t lastDay = 0;
uint8_t lastHour = 0;
uint8_t lastMinute = 0;
uint8_t lastFiveMinute = 0;
time_t lastTime = 0;
uint8_t randomHour = 0;
uint8_t randomMinute = 0;
uint8_t testColumn = 0;
uint8_t brightness = settings.getBrightness();
String yahooTitle = "";
int8_t yahooTemperature = 0;
uint8_t yahooHumidity = 0;
uint8_t yahooCode = 0;
uint8_t nextWeatherCheck = 30;
String updateInfo = "";
#ifdef BUZZER
boolean timerSet = false;
time_t timer = 0;
uint8_t timerMinutes = 0;
uint8_t alarmOn = 0;
#endif
#ifdef LDR
uint8_t ratedBrightness = 0;
uint16_t ldrValue = 0;
uint16_t lastLdrValue = 0;
uint16_t minLdrValue = 512;
uint16_t maxLdrValue = 512;
uint32_t lastLdrCheck = 0;
uint32_t lastBrightnessSet = 0;
#endif
#ifdef SENSOR_DHT22
DHT dht(PIN_DHT22, DHT22);
float dhtHumidity = 0;
float dhtTemperature = 0;
#endif
#ifdef IR_REMOTE
IRrecv irrecv(PIN_IR_RECEIVER);
decode_results irDecodeResult;
#endif
#ifdef SYSLOG_SERVER
WiFiUDP Udp;
Syslog syslog(Udp, SYSLOG_SERVER, SYSLOG_PORT, HOSTNAME, "QLOCKWORK", LOG_KERN);
#endif

/******************************************************************************
Setup().
******************************************************************************/

void setup()
{
	// Init serial port.
	Serial.begin(SERIAL_SPEED);
	while (!Serial);

	// Here we go...
	DEBUG_PRINTLN();
	DEBUG_PRINTLN("QLOCKWORK");
	DEBUG_PRINTLN("Firmware: " + String(FIRMWARE_VERSION));
	DEBUG_PRINTLN("LED-Driver: " + ledDriver.getSignature());
#ifdef LED_LAYOUT_HORIZONTAL
	DEBUG_PRINT("LED-Layout: horizontal");
#endif
#ifdef LED_LAYOUT_VERTICAL
	DEBUG_PRINT("LED-Layout: vertical");
#endif
#ifdef LED_LAYOUT_DUAL
	DEBUG_PRINTLN("LED-Layout: dual");
#else
	DEBUG_PRINTLN();
#endif

	// Init LED, Buzzer and LDR.

#ifdef ESP_LED
	DEBUG_PRINTLN("Setting up ESP-LED.");
	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, HIGH);
#endif

#ifdef BUZZER
	DEBUG_PRINTLN("Setting up Buzzer.");
	pinMode(PIN_BUZZER, OUTPUT);
#endif

#ifdef SENSOR_DHT22
	DEBUG_PRINTLN("Setting up DHT22.");
	dht.begin();
#endif

#ifdef LDR
	DEBUG_PRINT("Setting up LDR. LDR is ");
	if (settings.getUseLdr())
		DEBUG_PRINTLN("enabled.");
	else
		DEBUG_PRINTLN("disabled.");
	pinMode(PIN_LDR, INPUT);
	randomSeed(analogRead(PIN_LDR));
#endif

#ifdef SELFTEST
	renderer.setAllScreenBuffer(matrix);
	DEBUG_PRINTLN("Set all LEDs to white.");
	writeScreenBuffer(matrix, WHITE, constrain(120, 0, MAX_BRIGHTNESS)); // to many amps at 100% brightness
	delay(2000);
	DEBUG_PRINTLN("Set all LEDs to red.");
	writeScreenBuffer(matrix, RED, MAX_BRIGHTNESS);
	delay(2000);
	DEBUG_PRINTLN("Set all LEDs to green.");
	writeScreenBuffer(matrix, GREEN, MAX_BRIGHTNESS);
	delay(2000);
	DEBUG_PRINTLN("Set all LEDs to blue.");
	writeScreenBuffer(matrix, BLUE, MAX_BRIGHTNESS);
	delay(2000);
#endif

	// Start WiFi and services.

	renderer.clearScreenBuffer(matrix);
	renderer.setSmallText("WI", TEXT_POS_TOP, matrix);
	renderer.setSmallText("FI", TEXT_POS_BOTTOM, matrix);
	writeScreenBuffer(matrix, WHITE, brightness);
	WiFiManager wifiManager;
#ifdef WIFI_RESET
	wifiManager.resetSettings();
#endif
	wifiManager.setTimeout(WIFI_AP_TIMEOUT);
	wifiManager.autoConnect(HOSTNAME);
	if (WiFi.status() != WL_CONNECTED)
	{
		DEBUG_PRINTLN("WiFi not connected. Shutting down WiFi.");
		WiFi.mode(WIFI_OFF);
		writeScreenBuffer(matrix, RED, brightness);
		digitalWrite(PIN_BUZZER, HIGH);
		delay(1500);
		digitalWrite(PIN_BUZZER, LOW);
		delay(2000);
	}
	else
	{
		writeScreenBuffer(matrix, GREEN, brightness);
		for (uint8_t i = 0; i <= 2; i++)
		{
			digitalWrite(PIN_BUZZER, HIGH);
			delay(100);
			digitalWrite(PIN_BUZZER, LOW);
			delay(100);
		}
		delay(500);

#ifdef SHOW_IP
		showFeed("IP: " + String(WiFi.localIP()[0]) + '.' + String(WiFi.localIP()[1]) + '.' + String(WiFi.localIP()[2]) + '.' + String(WiFi.localIP()[3]), WHITE);
#endif
	}

	renderer.clearScreenBuffer(matrix);

	SYSLOG("QLOCKWORK");
	SYSLOG("Firmware: " + String(FIRMWARE_VERSION));
	SYSLOG("LED-Driver: " + ledDriver.getSignature());
#ifdef LED_LAYOUT_HORIZONTAL
	SYSLOG("LED-Layout: horizontal");
#endif
#ifdef LED_LAYOUT_VERTICAL
	SYSLOG("LED-Layout: vertical");
#endif
#ifdef LED_LAYOUT_DUAL
	SYSLOG("LED-Layout: dual");
#endif

	DEBUG_PRINTLN("Starting mDNS responder.");
	SYSLOG("Starting mDNS responder.");
	MDNS.begin(HOSTNAME);
	MDNS.addService("http", "tcp", 80);

	DEBUG_PRINTLN("Starting webserver on port 80.");
	SYSLOG("Starting webserver on port 80.");
	setupWebServer();

	DEBUG_PRINTLN("Starting updateserver on port 80.");
	SYSLOG("Starting updateserver on port 80.");
	httpUpdater.setup(&esp8266WebServer);

	DEBUG_PRINTLN("Starting OTA service.");
	SYSLOG("Starting OTA service.");
#ifdef SYSLOG_SERVER
	ArduinoOTA.onStart([]()
	{
		syslog.log(LOG_INFO, "Starting OTA.");
	});
	ArduinoOTA.onError([](ota_error_t error)
	{
		syslog.log(LOG_INFO, "OTA Error: " + String(error));
		if (error == OTA_AUTH_ERROR) syslog.log(LOG_INFO, "Auth failed.");
		else if (error == OTA_BEGIN_ERROR) syslog.log(LOG_INFO, "Begin failed.");
		else if (error == OTA_CONNECT_ERROR) syslog.log(LOG_INFO, "Connect failed.");
		else if (error == OTA_RECEIVE_ERROR) syslog.log(LOG_INFO, "Receive failed.");
		else if (error == OTA_END_ERROR) syslog.log(LOG_INFO, "End failed.");
	});
	ArduinoOTA.onEnd([]()
	{
		syslog.log(LOG_INFO, "End OTA.");
	});
#endif
	ArduinoOTA.setPassword(OTA_PASS);
	ArduinoOTA.begin();

#ifdef IR_REMOTE
	DEBUG_PRINTLN("Starting IR-receiver.");
	SYSLOG("Starting IR-receiver.");
	irrecv.enableIRIn();
#endif

	// Set some random values.
	randomHour = random(0, 24);
	DEBUG_PRINTLN("Random hour is: " + String(randomHour));
	SYSLOG("Random hour is: " + String(randomHour));

	randomMinute = random(1, 60);
	DEBUG_PRINTLN("Random minute is: " + String(randomMinute));
	SYSLOG("Random minute is: " + String(randomMinute));

	DEBUG_PRINTLN("Defined events: " + String(sizeof(event) / sizeof(event_t)));

	DEBUG_PRINTLN("Free RAM: " + String(system_get_free_heap_size()));
	SYSLOG("Free RAM: " + String(system_get_free_heap_size()));

	// Set ESP time at startup.
#ifdef RTC_BACKUP
#ifdef DEBUG
	debug.debugTime("Time (RTC):", RTC.get(), SHOW_DATE);
#endif
	DEBUG_PRINTLN("*** ESP set from RTC. ***");
	setTime(RTC.get());
#else
	DEBUG_PRINTLN("*** ESP time can not be set. ***");
	setTime(12, 00, 00, 01, 01, 2017);
#endif

	// Get time and weatherinfo from the internet.
	getNtpTime();
	getWeather(LOCATION);
	//getUpdateInfo();

	// Set some variables to startvalues.
	lastDay = day();
	lastHour = hour();
	lastFiveMinute = minute() / 5;
	lastMinute = minute();
	lastTime = now();
}

/******************************************************************************
Loop().
******************************************************************************/

void loop()
{
	// Run once a day.

	if (day() != lastDay)
	{
		lastDay = day();
		screenBufferNeedsUpdate = true;

		// Change color.
		if (settings.getColorChange() == COLORCHANGE_DAY)
		{
			settings.setColor(random(0, COLORCHANGE_COUNT + 1));
			DEBUG_PRINTLN("Color changed to: " + String(settings.getColor()));
			SYSLOG("Color changed to: " + String(settings.getColor()));
		}
	}

	// Run once every hour.

	if (hour() != lastHour)
	{
		lastHour = hour();
		screenBufferNeedsUpdate = true;

		DEBUG_PRINTLN("Free RAM: " + String(system_get_free_heap_size()));
		SYSLOG("Free RAM: " + String(system_get_free_heap_size()));

#if defined(UPDATE_INFO_STABLE) || defined(UPDATE_INFO_UNSTABLE)
		// Get updateinfo once a day at a random hour.
		//if ((hour() / float(randomHour)) == 1.0)
		if (hour() == randomHour)
		{
			getUpdateInfo();
		}
#endif

		// Change color.
		if (settings.getColorChange() == COLORCHANGE_HOUR)
		{
			settings.setColor(random(0, COLOR_COUNT + 1));
			DEBUG_PRINTLN("Color changed to: " + String(settings.getColor()));
			SYSLOG("Color changed to: " + String(settings.getColor()));
		}
	}

	// Run once every five minutes.

	if ((minute() / 5) != lastFiveMinute)
	{
		lastFiveMinute = minute() / 5;
		screenBufferNeedsUpdate = true;

#ifdef SYSLOG_SERVER
		syslog.logf(LOG_INFO, "Time (ESP): %02d:%02d:%02d %02d.%02d.%04d", hour(now()), minute(now()), second(now()), day(now()), month(now()), year(now()));
#endif

		// Show event in feed.
		for (uint8_t i = 0; i < (sizeof(event) / sizeof(event_t)); i++)
		{
			if ((day() == event[i].day) && (month() == event[i].month) && (mode == STD_MODE_TIME))
			{
				if (event[i].year)
				{
					showFeed(event[i].text + " (" + String(year() - event[i].year) + ")", event[i].color);
				}
				else
				{
					showFeed(event[i].text, event[i].color);
				}
			}
		}

		// Change color.
		if (settings.getColorChange() == COLORCHANGE_FIVE)
		{
			settings.setColor(random(0, COLOR_COUNT + 1));
			DEBUG_PRINTLN("Color changed to: " + String(settings.getColor()));
			SYSLOG("Color changed to: " + String(settings.getColor()));
		}
	}

	// Run once every minute.

	if (minute() != lastMinute)
	{
		lastMinute = minute();
		screenBufferNeedsUpdate = true;

#ifdef DEBUG
		debug.debugTime("Time (ESP):", now(), SHOW_DATE);
#endif

		// Set nightmode/daymode.
		if ((hour() == hour(settings.getNightOffTime())) && (minute() == minute(settings.getNightOffTime())))
		{
			DEBUG_PRINTLN("Night off.");
			SYSLOG("Night off.");
			setMode(STD_MODE_BLANK);
		}

		if ((hour() == hour(settings.getDayOnTime())) && (minute() == minute(settings.getDayOnTime())))
		{
			DEBUG_PRINTLN("Day on.");
			SYSLOG("Day on.");
			setMode(lastMode);
		}

		// Set ESP time from external source or RTC.
		if (minute() == randomMinute)
		{
			getNtpTime();
		}
#ifdef RTC_BACKUP
		else
		{
			if ((now() != RTC.get()) && (minute() != randomMinute) && (minute() != 0))
			{
				DEBUG_PRINTLN("*** ESP set from RTC. ***");
				DEBUG_PRINTLN("Drift (ESP/RTC): " + String(RTC.get() - now()) + " sec.");
				SYSLOG("*** ESP set from RTC. ***");
				SYSLOG("Drift (ESP/RTC): " + String(RTC.get() - now()) + " sec.");
				setTime(RTC.get());
			}
		}
#endif

		// Countdown to weatherinfo check.
		if (nextWeatherCheck)
		{
			nextWeatherCheck--;
			DEBUG_PRINTLN("Weathercheck locked for " + String(nextWeatherCheck) + " minutes.");
			SYSLOG("Weathercheck locked for " + String(nextWeatherCheck) + " minutes.");
		}
	}

	// Run once every second.

	if (now() != lastTime)
	{
		lastTime = now();

		// Running displayupdate in STD_MODE_TIME or STD_MODE_BLANK every second will lock the ESP due to TRANSITION_FADE.
		if ((mode != STD_MODE_TIME) && (mode != STD_MODE_BLANK))
		{
			screenBufferNeedsUpdate = true;
		}

#ifdef ESP_LED
		// Flash ESP LED.
		if (digitalRead(PIN_LED) == LOW)
		{
			digitalWrite(PIN_LED, HIGH);
		}
		else
		{
			digitalWrite(PIN_LED, LOW);
		}
#endif

#ifdef BUZZER
		// Switch on alarm for alarm1.
		if (settings.getAlarm1() && (hour() == hour(settings.getAlarmTime1())) && (minute() == minute(settings.getAlarmTime1())) && (second() == 0))
		{
			alarmOn = BUZZTIME_ALARM_1;
			DEBUG_PRINTLN("Alarm 1: on");
			SYSLOG("Alarm 1: on");
		}

		// Switch on alarm for alarm2.
		if (settings.getAlarm2() && (hour() == hour(settings.getAlarmTime2())) && (minute() == minute(settings.getAlarmTime2())) && (second() == 0))
		{
			alarmOn = BUZZTIME_ALARM_2;
			DEBUG_PRINTLN("Alarm 2: on");
			SYSLOG("Alarm 2: on");
		}

		// Switch on alarm for timer.
		if (timerSet && (now() == timer))
		{
			setMode(STD_MODE_SET_TIMER);
			timerMinutes = 0;
			timerSet = false;
			alarmOn = BUZZTIME_TIMER;
			DEBUG_PRINTLN("Timeralarm: on");
			SYSLOG("Timeralarm: on");
		}

		// Make some noise.
		if (alarmOn)
		{
			alarmOn--;
			if (digitalRead(PIN_BUZZER) == LOW)
			{
				digitalWrite(PIN_BUZZER, HIGH);
			}
			else
			{
				digitalWrite(PIN_BUZZER, LOW);
			}
		}
		else
		{
			digitalWrite(PIN_BUZZER, LOW);
		}
#endif

		// Show temperatures.
		if (settings.getShowTemp() && (mode == STD_MODE_TIME))
		{
			autoModeChange--;
			if (!autoModeChange)
			{
				autoModeChange = AUTO_MODECHANGE_TIME;
				setMode(STD_MODE_EXT_TEMP);
			}
		}
	}

	// Run always.

	// Call HTTP- and OTA-handle.
	esp8266WebServer.handleClient();
	ArduinoOTA.handle();

#ifdef LDR
	// Set brigthness from LDR.
	setBrightnessFromLdr();
#endif

#ifdef IR_REMOTE
	// Look for IR commands.
	if (irrecv.decode(&irDecodeResult))
	{
#ifdef DEBUG
		Serial.print("IR signal: ");
		serialPrintUint64(irDecodeResult.value, DEC);
		Serial.println();
#endif
		remoteAction(irDecodeResult);
		irrecv.resume();
	}
#endif

	// Render a new screenbuffer if needed.
	if (screenBufferNeedsUpdate)
	{
		screenBufferNeedsUpdate = false;

		// Save old screenbuffer.
		for (uint8_t i = 0; i <= 9; i++)
		{
			matrixOld[i] = matrix[i];
		}

		// Render a new screenbuffer.
		switch (mode)
		{
		case STD_MODE_TIME:
			renderer.clearScreenBuffer(matrix);
			renderer.setTime(hour(), minute(), settings.getLanguage(), matrix);
			renderer.setCorners(minute(), matrix);
#ifdef BUZZER
			if (settings.getAlarm1() || settings.getAlarm2() || timerSet)
			{
				renderer.setAlarmLed(matrix);
			}
			if (!settings.getItIs() && ((minute() / 5) % 6))
			{
				renderer.clearEntryWords(settings.getLanguage(), matrix);
			}
#endif
			break;
		case STD_MODE_AMPM:
			renderer.clearScreenBuffer(matrix);
			if (isAM())
			{
				renderer.setSmallText("AM", TEXT_POS_MIDDLE, matrix);
			}
			else
			{
				renderer.setSmallText("PM", TEXT_POS_MIDDLE, matrix);
			}
			break;
		case STD_MODE_SECONDS:
			renderer.clearScreenBuffer(matrix);
			renderer.setCorners(minute(), matrix);
			for (uint8_t i = 0; i <= 6; i++)
			{
				matrix[1 + i] |= numbersBig[second() / 10][i] << 11;
				matrix[1 + i] |= numbersBig[second() % 10][i] << 5;
			}
			break;
		case STD_MODE_WEEKDAY:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText(String(sWeekday[weekday()][0]) + String(sWeekday[weekday()][1]), TEXT_POS_MIDDLE, matrix);
			break;
		case STD_MODE_DATE:
			renderer.clearScreenBuffer(matrix);
			if (day() < 10)
			{
				renderer.setSmallText(("0" + String(day())), TEXT_POS_TOP, matrix);
			}
			else
			{
				renderer.setSmallText(String(day()), TEXT_POS_TOP, matrix);
			}
			if (month() < 10)
			{
				renderer.setSmallText(("0" + String(month())), TEXT_POS_BOTTOM, matrix);
			}
			else
			{
				renderer.setSmallText(String(month()), TEXT_POS_BOTTOM, matrix);
			}
			renderer.setPixelInScreenBuffer(5, 4, matrix);
			renderer.setPixelInScreenBuffer(5, 9, matrix);
			break;
		case STD_MODE_TITLE_TEMP:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("TE", TEXT_POS_TOP, matrix);
			renderer.setSmallText("MP", TEXT_POS_BOTTOM, matrix);
			break;
#if defined(RTC_BACKUP) && !defined(SENSOR_DHT22)
		case STD_MODE_TEMP:
			renderer.clearScreenBuffer(matrix);
			if ((RTC.temperature() / 4) == 0)
			{
				matrix[0] = 0b0000000001000000;
				matrix[1] = 0b0000000010100000;
				matrix[2] = 0b0000000010100000;
				matrix[3] = 0b0000000011100000;
			}
			if ((RTC.temperature() / 4) > 0)
			{
				matrix[0] = 0b0000000001000000;
				matrix[1] = 0b0100000010100000;
				matrix[2] = 0b1110000010100000;
				matrix[3] = 0b0100000011100000;
			}
			if ((RTC.temperature() / 4) < 0)
			{
				matrix[0] = 0b0000000001000000;
				matrix[1] = 0b0000000010100000;
				matrix[2] = 0b1110000010100000;
				matrix[3] = 0b0000000011100000;
			}
			renderer.setSmallText(String(int(RTC.temperature() / 4.0 + 0.5)), TEXT_POS_BOTTOM, matrix);
			DEBUG_PRINTLN(String(RTC.temperature() / 4.0)); // .0 to get float value, +0.5 for rounding.
			break;
#endif
#ifdef SENSOR_DHT22
		case STD_MODE_TEMP:
			renderer.clearScreenBuffer(matrix);
			dhtTemperature = getDHT22Temperature();
			if (dhtTemperature == 0)
			{
				matrix[0] = 0b0000000001000000;
				matrix[1] = 0b0000000010100000;
				matrix[2] = 0b0000000010100000;
				matrix[3] = 0b0000000011100000;
			}
			if (dhtTemperature > 0)
			{
				matrix[0] = 0b0000000001000000;
				matrix[1] = 0b0100000010100000;
				matrix[2] = 0b1110000010100000;
				matrix[3] = 0b0100000011100000;
			}
			if (dhtTemperature < 0)
			{
				matrix[0] = 0b0000000001000000;
				matrix[1] = 0b0000000010100000;
				matrix[2] = 0b1110000010100000;
				matrix[3] = 0b0000000011100000;
			}
			renderer.setSmallText(String(int(dhtTemperature + 0.5)), TEXT_POS_BOTTOM, matrix);
			DEBUG_PRINTLN("DHT22 temperature: " + String(dhtTemperature));
			break;
		case STD_MODE_HUMIDITY:
			renderer.clearScreenBuffer(matrix);
			dhtHumidity = getDHT22Humidity();
			renderer.setSmallText(String(int(dhtHumidity + 0.5)), TEXT_POS_TOP, matrix);
			DEBUG_PRINTLN("DHT22 humidity: " + String(dhtHumidity));
			matrix[6] = 0b0100100001000000;
			matrix[7] = 0b0001000010100000;
			matrix[8] = 0b0010000010100000;
			matrix[9] = 0b0100100011100000;
			break;
#endif
		case STD_MODE_EXT_TEMP:
			renderer.clearScreenBuffer(matrix);
			// Refresh weatherinfo?
			if (nextWeatherCheck == 0)
			{
				nextWeatherCheck = 30;
				getWeather(LOCATION);
			}
			if (yahooTemperature > 0)
			{
				matrix[1] = 0b0100000000000000;
				matrix[2] = 0b1110000000000000;
				matrix[3] = 0b0100000000000000;
			}
			if (yahooTemperature < 0)
			{
				matrix[2] = 0b1110000000000000;
			}
			renderer.setSmallText(String(yahooTemperature), TEXT_POS_BOTTOM, matrix);
			break;
		case STD_MODE_EXT_HUMIDITY:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText(String(yahooHumidity), TEXT_POS_TOP, matrix);
			matrix[6] = 0b0100100000000000;
			matrix[7] = 0b0001000000000000;
			matrix[8] = 0b0010000000000000;
			matrix[9] = 0b0100100000000000;
			break;
#ifdef BUZZER
		case STD_MODE_TITLE_ALARM:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("AL", TEXT_POS_TOP, matrix);
			renderer.setSmallText("RM", TEXT_POS_BOTTOM, matrix);
			break;
		case STD_MODE_SET_TIMER:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("TI", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				renderer.deactivateAlarmLed(matrix);
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				renderer.setAlarmLed(matrix);
				renderer.setSmallText(String(timerMinutes), TEXT_POS_BOTTOM, matrix);
			}
			break;
		case STD_MODE_TIMER:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("TI", TEXT_POS_TOP, matrix);
			renderer.setSmallText(String((timer - now() + 60) / 60), TEXT_POS_BOTTOM, matrix);
			DEBUG_PRINTLN(String(timer - now()));
			break;
		case STD_MODE_ALARM_1:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("A1", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getAlarm1())
				{
					renderer.setSmallText("EN", TEXT_POS_BOTTOM, matrix);
					renderer.setAlarmLed(matrix);
				}
				else
				{
					renderer.setSmallText("DA", TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
		case STD_MODE_SET_ALARM_1:
			renderer.clearScreenBuffer(matrix);
			if (second() % 2 == 0)
			{
				renderer.setTime(hour(settings.getAlarmTime1()), minute(settings.getAlarmTime1()), settings.getLanguage(), matrix);
				renderer.clearEntryWords(settings.getLanguage(), matrix);
				renderer.setAMPM(hour(settings.getAlarmTime1()), settings.getLanguage(), matrix);
				renderer.setAlarmLed(matrix);
			}
			break;
		case STD_MODE_ALARM_2:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("A2", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getAlarm2())
				{
					renderer.setSmallText("EN", TEXT_POS_BOTTOM, matrix);
					renderer.setAlarmLed(matrix);
				}
				else
					renderer.setSmallText("DA", TEXT_POS_BOTTOM, matrix);
			}
			break;
		case STD_MODE_SET_ALARM_2:
			renderer.clearScreenBuffer(matrix);
			if (second() % 2 == 0)
			{
				renderer.setTime(hour(settings.getAlarmTime2()), minute(settings.getAlarmTime2()), settings.getLanguage(), matrix);
				renderer.clearEntryWords(settings.getLanguage(), matrix);
				renderer.setAMPM(hour(settings.getAlarmTime2()), settings.getLanguage(), matrix);
				renderer.setAlarmLed(matrix);
			}
			break;
#endif
		case EXT_MODE_TITLE_MAIN:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("MA", TEXT_POS_TOP, matrix);
			renderer.setSmallText("IN", TEXT_POS_BOTTOM, matrix);
			break;
#ifdef LDR
		case EXT_MODE_LDR:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("LD", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getUseLdr())
				{
					renderer.setSmallText("EN", TEXT_POS_BOTTOM, matrix);
				}
				else
				{
					renderer.setSmallText("DA", TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
#endif
		case EXT_MODE_BRIGHTNESS:
			renderer.clearScreenBuffer(matrix);
			for (uint8_t x = 0; x < map(settings.getBrightness(), 0, 255, 1, 10); x++)
			{
				for (uint8_t y = 0; y <= x; y++)
				{
					matrix[9 - y] |= 1 << (14 - x);
				}
			}
			break;
		case EXT_MODE_COLORCHANGE:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("CC", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getColorChange() == COLORCHANGE_NO)
				{
					renderer.setSmallText("NO", TEXT_POS_BOTTOM, matrix);
				}
				if (settings.getColorChange() == COLORCHANGE_FIVE)
				{
					renderer.setSmallText("FI", TEXT_POS_BOTTOM, matrix);
				}
				if (settings.getColorChange() == COLORCHANGE_HOUR)
				{
					renderer.setSmallText("HR", TEXT_POS_BOTTOM, matrix);
				}
				if (settings.getColorChange() == COLORCHANGE_DAY)
				{
					renderer.setSmallText("DY", TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
		case EXT_MODE_COLOR:
			renderer.clearScreenBuffer(matrix);
			matrix[0] = 0b0000000000010000;
			matrix[1] = 0b0000000000010000;
			matrix[2] = 0b0000000000010000;
			matrix[3] = 0b0000000000010000;
			matrix[4] = 0b0000000000000000;
			renderer.setSmallText("CO", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				renderer.setSmallText(String(settings.getColor()), TEXT_POS_BOTTOM, matrix);
			}
			break;
		case EXT_MODE_TRANSITION:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("TR", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getTransition() == TRANSITION_NORMAL)
				{
					renderer.setSmallText("NO", TEXT_POS_BOTTOM, matrix);
				}
				if (settings.getTransition() == TRANSITION_FADE)
				{
					renderer.setSmallText("FD", TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
		case EXT_MODE_TIMEOUT:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("FB", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				renderer.setSmallText(String(settings.getTimeout()), TEXT_POS_BOTTOM, matrix);
			}
			break;
		case EXT_MODE_SHOW_TEMP:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("ST", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getShowTemp())
				{
					renderer.setSmallText("EN", TEXT_POS_BOTTOM, matrix);
				}
				else
				{
					renderer.setSmallText("DA", TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
		case EXT_MODE_LANGUAGE:
			renderer.clearScreenBuffer(matrix);
			if (second() % 2 == 0)
			{
				if (sLanguage[settings.getLanguage()][3] == ' ')
				{
					renderer.setSmallText(String(sLanguage[settings.getLanguage()][0]) + String(sLanguage[settings.getLanguage()][1]), TEXT_POS_MIDDLE, matrix);
				}
				else
				{
					renderer.setSmallText(String(sLanguage[settings.getLanguage()][0]) + String(sLanguage[settings.getLanguage()][1]), TEXT_POS_TOP, matrix);
					renderer.setSmallText(String(sLanguage[settings.getLanguage()][2]) + String(sLanguage[settings.getLanguage()][3]), TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
		case EXT_MODE_TITLE_TIME:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("TI", TEXT_POS_TOP, matrix);
			renderer.setSmallText("ME", TEXT_POS_BOTTOM, matrix);
			break;
		case EXT_MODE_TIMESET:
			renderer.clearScreenBuffer(matrix);
			if (second() % 2 == 0)
			{
				renderer.setTime(hour(), minute(), settings.getLanguage(), matrix);
				renderer.setCorners(minute(), matrix);
				renderer.clearEntryWords(settings.getLanguage(), matrix);
				renderer.setAMPM(hour(), settings.getLanguage(), matrix);
			}
			break;
		case EXT_MODE_IT_IS:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("IT", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				if (settings.getItIs())
				{
					renderer.setSmallText("EN", TEXT_POS_BOTTOM, matrix);
				}
				else
				{
					renderer.setSmallText("DA", TEXT_POS_BOTTOM, matrix);
				}
			}
			break;
		case EXT_MODE_DAYSET:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("DD", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				renderer.setSmallText(String(day()), TEXT_POS_BOTTOM, matrix);
			}
			break;
		case EXT_MODE_MONTHSET:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("MM", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				renderer.setSmallText(String(month()), TEXT_POS_BOTTOM, matrix);
			}
			break;
		case EXT_MODE_YEARSET:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("YY", TEXT_POS_TOP, matrix);
			if (second() % 2 == 0)
			{
				for (uint8_t i = 5; i <= 9; i++)
				{
					matrix[i] = 0;
				}
			}
			else
			{
				renderer.setSmallText(String(year() % 100), TEXT_POS_BOTTOM, matrix);
			}
			break;
		case EXT_MODE_TEXT_NIGHTOFF:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("NT", TEXT_POS_TOP, matrix);
			renderer.setSmallText("OF", TEXT_POS_BOTTOM, matrix);
			break;
		case EXT_MODE_NIGHTOFF:
			renderer.clearScreenBuffer(matrix);
			if (second() % 2 == 0)
			{
				renderer.setTime(hour(settings.getNightOffTime()), minute(settings.getNightOffTime()), settings.getLanguage(), matrix);
				renderer.clearEntryWords(settings.getLanguage(), matrix);
				renderer.setAMPM(hour(settings.getNightOffTime()), settings.getLanguage(), matrix);
			}
			break;
		case EXT_MODE_TEXT_DAYON:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("DY", TEXT_POS_TOP, matrix);
			renderer.setSmallText("ON", TEXT_POS_BOTTOM, matrix);
			break;
		case EXT_MODE_DAYON:
			renderer.clearScreenBuffer(matrix);
			if (second() % 2 == 0)
			{
				renderer.setTime(hour(settings.getDayOnTime()), minute(settings.getDayOnTime()), settings.getLanguage(), matrix);
				renderer.clearEntryWords(settings.getLanguage(), matrix);
				renderer.setAMPM(hour(settings.getDayOnTime()), settings.getLanguage(), matrix);
			}
			break;
		case EXT_MODE_TITLE_TEST:
			renderer.clearScreenBuffer(matrix);
			renderer.setSmallText("TE", TEXT_POS_TOP, matrix);
			renderer.setSmallText("ST", TEXT_POS_BOTTOM, matrix);
			break;
		case EXT_MODE_TEST:
			renderer.clearScreenBuffer(matrix);
			if (testColumn == 10)
				testColumn = 0;
			matrix[testColumn] = 0b1111111111110000;
			testColumn++;
			break;
		case STD_MODE_BLANK:
			renderer.clearScreenBuffer(matrix);
			break;
		default:
			break;
			}

#if defined(IR_LETTER_OFF)
		// Turn off LED behind IR-sensor.
		renderer.unsetPixelInScreenBuffer(8, 9, matrix);
#endif

#ifdef DEBUG_MATRIX
		//debug.debugScreenBuffer(matrixOld);
		debug.debugScreenBuffer(matrix);
#endif

		// Write screenbuffer to display.
		switch (mode)
		{
		case STD_MODE_TIME:
		case STD_MODE_BLANK:
			if (settings.getTransition() == TRANSITION_NORMAL)
				writeScreenBuffer(matrix, settings.getColor(), brightness);
			if (settings.getTransition() == TRANSITION_FADE)
				writeScreenBufferFade(matrixOld, matrix, settings.getColor(), brightness);
			break;
		default:
			if (!runTransitionOnce)
			{
				moveScreenBufferUp(matrixOld, matrix, settings.getColor(), brightness);
				runTransitionOnce = true;
			}
			else
			{
				writeScreenBuffer(matrix, settings.getColor(), brightness);
			}
			break;
		}
	}

	// Wait for mode timeout then switch back to time.
	if ((millis() > (modeTimeout + settings.getTimeout() * 1000)) && modeTimeout)
	{
		setMode(STD_MODE_TIME);
	}

	// Wait for title timeout then set next mode.
	switch (mode)
	{
	case STD_MODE_TITLE_TEMP:
#ifdef BUZZER
	case STD_MODE_TITLE_ALARM:
#endif
	case EXT_MODE_TITLE_MAIN:
	case EXT_MODE_TITLE_TIME:
	case EXT_MODE_TEXT_NIGHTOFF:
	case EXT_MODE_TEXT_DAYON:
	case EXT_MODE_TITLE_TEST:
		if (millis() > titleTimeout + 2000)
		{
			setMode(mode++);
		}
		break;
	default:
		break;
	}

#ifdef DEBUG_FPS
	debug.debugFps();
#endif
}

/******************************************************************************
"On/off" pressed.
******************************************************************************/

void buttonOnOffPressed()
{
	DEBUG_PRINTLN("On/off pressed.");
	SYSLOG("On/off pressed.");

#ifdef BUZZER
	// Switch off alarm.
	if (alarmOn)
	{
		DEBUG_PRINTLN("Alarm off.");
		SYSLOG("Alarm off.");
		digitalWrite(PIN_BUZZER, LOW);
		alarmOn = false;
		setMode(STD_MODE_TIME);
	}
#endif

	setDisplayToToggle();
}

/******************************************************************************
"Mode" pressed.
******************************************************************************/

void buttonModePressed()
{
	titleTimeout = millis();

#ifdef BUZZER
	// Switch off alarm.
	if (alarmOn)
	{
		DEBUG_PRINTLN("Alarm off.");
		SYSLOG("Alarm off.");
		digitalWrite(PIN_BUZZER, LOW);
		alarmOn = false;
		setMode(STD_MODE_TIME);
		return;
	}
#endif

	// Set mode.
	setMode(mode++);
	switch (mode)
	{
	case STD_MODE_COUNT:
	case EXT_MODE_COUNT:
		setMode(STD_MODE_TIME);
		break;
	case EXT_MODE_COLOR:
		if (settings.getColorChange())
			setMode(mode++);
		break;
#ifdef BUZZER
	case STD_MODE_SET_TIMER:
		if (timerSet)
			setMode(mode++);
		break;
	case STD_MODE_TIMER:
		if (!timerSet)
			setMode(mode++);
		break;
	case STD_MODE_SET_ALARM_1:
		if (!settings.getAlarm1())
			setMode(mode++);
		break;
	case STD_MODE_SET_ALARM_2:
		if (!settings.getAlarm2())
			setMode(STD_MODE_TIME);
		break;
#endif
#ifdef LDR
	case EXT_MODE_BRIGHTNESS:
		if (settings.getUseLdr())
			setMode(mode++);
		break;
#endif
	case EXT_MODE_TEST:
		testColumn = 0;
		return;
	default:
		break;
	}

#ifdef RTC_BACKUP
	RTC.set(now());
#endif

	settings.saveToEEPROM();
}

/******************************************************************************
"Settings" pressed.
******************************************************************************/

void buttonSettingsPressed()
{
	DEBUG_PRINTLN("Settings pressed.");
	SYSLOG("Settings pressed.");
	titleTimeout = millis();

#ifdef BUZZER
	// Switch off alarm.
	if (alarmOn)
	{
		DEBUG_PRINTLN("Alarm off.");
		SYSLOG("Alarm off.");
		digitalWrite(PIN_BUZZER, LOW);
		alarmOn = false;
		setMode(STD_MODE_TIME);
		return;
	}
#endif

	if (mode < EXT_MODE_START)
	{
		setMode(EXT_MODE_START);
	}
	else
	{
		buttonModePressed();
	}
}

/******************************************************************************
"Time" pressed.
******************************************************************************/

void buttonTimePressed()
{
	DEBUG_PRINTLN("Time pressed.");
	SYSLOG("Time pressed.");

#ifdef BUZZER
	// Switch off alarm.
	if (alarmOn)
	{
		DEBUG_PRINTLN("Alarm off.");
		SYSLOG("Alarm off.");
		digitalWrite(PIN_BUZZER, LOW);
		alarmOn = false;
		setMode(STD_MODE_TIME);
		return;
	}
#endif

#ifdef RTC_BACKUP
	RTC.set(now());
#endif

	settings.saveToEEPROM();
	modeTimeout = 0;
	renderer.clearScreenBuffer(matrix);
	setMode(STD_MODE_TIME);
}

/******************************************************************************
"Plus" pressed.
******************************************************************************/

void buttonPlusPressed()
{
	DEBUG_PRINTLN("Plus pressed.");
	SYSLOG("Plus pressed.");
	screenBufferNeedsUpdate = true;
	titleTimeout = millis();

#ifdef BUZZER
	// Switch off alarm.
	if (alarmOn)
	{
		DEBUG_PRINTLN("Alarm off.");
		SYSLOG("Alarm off.");
		digitalWrite(PIN_BUZZER, LOW);
		alarmOn = false;
		return;
	}
#endif

	switch (mode)
	{
	case STD_MODE_TIME:
		setMode(STD_MODE_TITLE_TEMP);
		break;
#ifdef BUZZER
	case STD_MODE_TITLE_TEMP:
		setMode(STD_MODE_TITLE_ALARM);
		break;
	case STD_MODE_TITLE_ALARM:
		setMode(STD_MODE_TIME);
		break;
	case STD_MODE_SET_TIMER:
		if (timerMinutes < 100)
			timerMinutes++;
		timer = now() + timerMinutes * 60;
		timerSet = true;
		break;
	case STD_MODE_TIMER:
		timerSet = false;
		setMode(STD_MODE_SET_TIMER);
		break;
	case STD_MODE_ALARM_1:
		settings.toggleAlarm1();
		break;
	case STD_MODE_SET_ALARM_1:
		settings.setAlarmTime1(settings.getAlarmTime1() + 3600);
#ifdef DEBUG
		debug.debugTime("Alarm 1:", settings.getAlarmTime1(), HIDE_DATE);
#endif
		break;
	case STD_MODE_ALARM_2:
		settings.toggleAlarm2();
		break;
	case STD_MODE_SET_ALARM_2:
		settings.setAlarmTime2(settings.getAlarmTime2() + 3600);
#ifdef DEBUG
		debug.debugTime("Alarm 2:", settings.getAlarmTime2(), HIDE_DATE);
#endif
#endif
		break;
	case EXT_MODE_TITLE_MAIN:
		setMode(EXT_MODE_TITLE_TIME);
		break;
#ifdef LDR
	case EXT_MODE_LDR:
		settings.toggleUseLdr();
		if (settings.getUseLdr())
			lastLdrValue = 0;
		else
			brightness = constrain(settings.getBrightness(), MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		break;
#endif
	case EXT_MODE_BRIGHTNESS:
		settings.setBrightness(constrain(settings.getBrightness() + 10, MIN_BRIGHTNESS, MAX_BRIGHTNESS));
		brightness = settings.getBrightness();
		writeScreenBuffer(matrix, settings.getColor(), brightness);
		DEBUG_PRINTLN(settings.getBrightness());
		break;
	case EXT_MODE_COLORCHANGE:
		if (settings.getColorChange() < COLORCHANGE_COUNT)
			settings.setColorChange(settings.getColorChange() + 1);
		else
			settings.setColorChange(0);
		break;
	case EXT_MODE_COLOR:
		if (settings.getColor() < COLOR_COUNT)
			settings.setColor(settings.getColor() + 1);
		else
			settings.setColor(0);
		break;
	case EXT_MODE_TRANSITION:
		if (settings.getTransition() < TRANSITION_COUNT)
			settings.setTransition(settings.getTransition() + 1);
		else
			settings.setTransition(0);
		break;
	case EXT_MODE_TIMEOUT:
		if (settings.getTimeout() < 99)
			settings.setTimeout(settings.getTimeout() + 1);
		break;
	case EXT_MODE_SHOW_TEMP:
		settings.toggleShowTemp();
		break;
	case EXT_MODE_LANGUAGE:
		if (settings.getLanguage() < LANGUAGE_COUNT)
			settings.setLanguage(settings.getLanguage() + 1);
		else
			settings.setLanguage(0);
		break;
	case EXT_MODE_TITLE_TIME:
		setMode(EXT_MODE_TITLE_TEST);
		break;
	case EXT_MODE_TIMESET:
		setTime(hour() + 1, minute(), second(), day(), month(), year());
#ifdef DEBUG
		debug.debugTime("Time set:", now(), SHOW_DATE);
#endif
		break;
	case EXT_MODE_IT_IS:
		settings.toggleItIs();
		break;
	case EXT_MODE_DAYSET:
		setTime(hour(), minute(), second(), day() + 1, month(), year());
		break;
	case EXT_MODE_MONTHSET:
		setTime(hour(), minute(), second(), day(), month() + 1, year());
		break;
	case EXT_MODE_YEARSET:
		setTime(hour(), minute(), second(), day(), month(), year() + 1);
		break;
	case EXT_MODE_TEXT_NIGHTOFF:
		setMode(EXT_MODE_NIGHTOFF);
		break;
	case EXT_MODE_NIGHTOFF:
		settings.setNightOffTime(settings.getNightOffTime() + 3600);
#ifdef DEBUG
		debug.debugTime("Night off:", settings.getNightOffTime(), HIDE_DATE);
#endif
		break;
	case EXT_MODE_TEXT_DAYON:
		setMode(EXT_MODE_DAYON);
		break;
	case EXT_MODE_DAYON:
		settings.setDayOnTime(settings.getDayOnTime() + 3600);
#ifdef DEBUG
		debug.debugTime("Day on:", settings.getDayOnTime(), HIDE_DATE);
#endif
		break;
	case EXT_MODE_TITLE_TEST:
		setMode(EXT_MODE_TITLE_MAIN);
		break;
	default:
		break;
	}
}

/******************************************************************************
"Minus" pressed
******************************************************************************/

void buttonMinusPressed()
{
	DEBUG_PRINTLN("Minus pressed.");
	SYSLOG("Minus pressed.");
	screenBufferNeedsUpdate = true;
	titleTimeout = millis();

#ifdef BUZZER
	// Switch off alarm.
	if (alarmOn)
	{
		DEBUG_PRINTLN("Alarm off.");
		SYSLOG("Alarm off.");
		digitalWrite(PIN_BUZZER, LOW);
		alarmOn = false;
		return;
	}
#endif

	switch (mode)
	{
	case STD_MODE_TITLE_TEMP:
		setMode(STD_MODE_TIME);
		break;
#ifdef BUZZER
	case STD_MODE_TITLE_ALARM:
		setMode(STD_MODE_TITLE_TEMP);
		break;
	case STD_MODE_TIME:
		setMode(STD_MODE_TITLE_ALARM);
		break;
	case STD_MODE_SET_TIMER:
		if (timerMinutes > 0)
		{
			timerMinutes--;
			if (timerMinutes == 0)
				timerSet = false;
			else
			{
				timer = now() + timerMinutes * 60;
				timerSet = true;
			}
		}
		break;
	case STD_MODE_TIMER:
		timerSet = false;
		setMode(STD_MODE_SET_TIMER);
		break;
	case STD_MODE_ALARM_1:
		settings.toggleAlarm1();
		break;
	case STD_MODE_SET_ALARM_1:
		settings.setAlarmTime1(settings.getAlarmTime1() + 300);
#ifdef DEBUG
		debug.debugTime("Alarm 1:", settings.getAlarmTime1(), HIDE_DATE);
#endif
		break;
	case STD_MODE_ALARM_2:
		settings.toggleAlarm2();
		break;
	case STD_MODE_SET_ALARM_2:
		settings.setAlarmTime2(settings.getAlarmTime2() + 300);
#ifdef DEBUG
		debug.debugTime("Alarm 2:", settings.getAlarmTime2(), HIDE_DATE);
#endif
#endif
		break;
	case EXT_MODE_TITLE_MAIN:
		setMode(EXT_MODE_TITLE_TEST);
		break;
#ifdef LDR
	case EXT_MODE_LDR:
		settings.toggleUseLdr();
		if (settings.getUseLdr())
			lastLdrValue = 0;
		else
			brightness = constrain(settings.getBrightness(), MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		break;
#endif
	case EXT_MODE_BRIGHTNESS:
		settings.setBrightness(constrain(settings.getBrightness() - 10, MIN_BRIGHTNESS, MAX_BRIGHTNESS));
		brightness = settings.getBrightness();
		writeScreenBuffer(matrix, settings.getColor(), brightness);
		DEBUG_PRINTLN(settings.getBrightness());
		break;
	case EXT_MODE_COLORCHANGE:
		if (settings.getColorChange() > 0)
			settings.setColorChange(settings.getColorChange() - 1);
		else
			settings.setColorChange(COLORCHANGE_COUNT);
		break;
	case EXT_MODE_COLOR:
		if (settings.getColor() > 0)
			settings.setColor(settings.getColor() - 1);
		else
			settings.setColor(COLOR_COUNT);
		break;
	case EXT_MODE_TRANSITION:
		if (settings.getTransition() > 0)
			settings.setTransition(settings.getTransition() - 1);
		else
			settings.setTransition(TRANSITION_COUNT);
		break;
	case EXT_MODE_TIMEOUT:
		if (settings.getTimeout() > 0)
			settings.setTimeout(settings.getTimeout() - 1);
		break;
	case EXT_MODE_SHOW_TEMP:
		settings.toggleShowTemp();
		break;
	case EXT_MODE_LANGUAGE:
		if (settings.getLanguage() > 0)
			settings.setLanguage(settings.getLanguage() - 1);
		else
			settings.setLanguage(LANGUAGE_COUNT);
		break;
	case EXT_MODE_TITLE_TIME:
		setMode(EXT_MODE_TITLE_MAIN);
		break;
	case EXT_MODE_TIMESET:
		setTime(hour(), minute() + 1, 0, day(), month(), year());
#ifdef DEBUG
		debug.debugTime("Time set:", now(), HIDE_DATE);
#endif
		break;
	case EXT_MODE_IT_IS:
		settings.toggleItIs();
		break;
	case EXT_MODE_DAYSET:
		setTime(hour(), minute(), second(), day() - 1, month(), year());
		break;
	case EXT_MODE_MONTHSET:
		setTime(hour(), minute(), second(), day(), month() - 1, year());
		break;
	case EXT_MODE_YEARSET:
		setTime(hour(), minute(), second(), day(), month(), year() - 1);
		break;
	case EXT_MODE_TEXT_NIGHTOFF:
		setMode(EXT_MODE_NIGHTOFF);
		break;
	case EXT_MODE_NIGHTOFF:
		settings.setNightOffTime(settings.getNightOffTime() + 300);
#ifdef DEBUG
		debug.debugTime("Night off:", settings.getNightOffTime(), HIDE_DATE);
#endif
		break;
	case EXT_MODE_TEXT_DAYON:
		setMode(EXT_MODE_DAYON);
		break;
	case EXT_MODE_DAYON:
		settings.setDayOnTime(settings.getDayOnTime() + 300);
#ifdef DEBUG
		debug.debugTime("Day on:", settings.getDayOnTime(), HIDE_DATE);
#endif
		break;
	case EXT_MODE_TITLE_TEST:
		setMode(EXT_MODE_TITLE_TIME);
		break;
	default:
		break;
	}
}

#ifdef IR_REMOTE
/******************************************************************************
IR-signal received.
******************************************************************************/

void remoteAction(decode_results irDecodeResult)
{
	switch (irDecodeResult.value)
	{
	case IR_CODE_ONOFF:
		buttonOnOffPressed();
		break;
	case IR_CODE_TIME:
		buttonTimePressed();
		break;
	case IR_CODE_MODE:
		buttonModePressed();
		break;
	case IR_CODE_SETTINGS:
		buttonSettingsPressed();
		break;
	case IR_CODE_PLUS:
		buttonPlusPressed();
		break;
	case IR_CODE_MINUS:
		buttonMinusPressed();
		break;
	default:
		break;
	}
}
#endif

/******************************************************************************
Transitions.
******************************************************************************/

void moveScreenBufferUp(uint16_t screenBufferOld[], uint16_t screenBufferNew[], uint8_t color, uint8_t brightness)
{
	esp8266WebServer.handleClient();
	for (uint8_t z = 0; z <= 9; z++)
	{
		for (uint8_t i = 0; i <= 8; i++)
		{
			screenBufferOld[i] = screenBufferOld[i + 1];
		}
		screenBufferOld[9] = screenBufferNew[z];
		writeScreenBuffer(screenBufferOld, color, brightness);
		delay(50);
	}
}

void writeScreenBuffer(uint16_t screenBuffer[], uint8_t color, uint8_t brightness)
{
	//DEBUG_PRINT("Running writeScreenBuffer.");
	ledDriver.clear();
	for (uint8_t y = 0; y <= 9; y++)
	{
		for (uint8_t x = 0; x <= 10; x++)
		{
			if (bitRead(screenBuffer[y], 15 - x))
			{
				ledDriver.setPixel(x, y, color, brightness);
			}
		}
	}

	// Corner LEDs.
	for (uint8_t y = 0; y <= 3; y++)
	{
		if (bitRead(screenBuffer[y], 4))
		{
			ledDriver.setPixel(110 + y, color, brightness);
		}
	}

#ifdef BUZZER
	// Alarm LED.
	if (bitRead(screenBuffer[4], 4))
	{
#ifdef ALARM_LED_COLOR
#ifdef ABUSE_CORNER_LED_FOR_ALARM
		if (settings.getAlarm1() || settings.getAlarm2() || timerSet)
		{
			ledDriver.setPixel(111, ALARM_LED_COLOR, brightness);
		}
		else
		{
			if (bitRead(screenBuffer[1], 4))
			{
				ledDriver.setPixel(111, color, brightness);
			}
		}
#else
		ledDriver.setPixel(114, ALARM_LED_COLOR, brightness);
#endif
#else
		ledDriver.setPixel(114, color, brightness);
#endif
	}
#endif

	ledDriver.show();
	//DEBUG_PRINTLN(" Done.");
	}

void writeScreenBufferFade(uint16_t screenBufferOld[], uint16_t screenBufferNew[], uint8_t color, uint8_t brightness)
{
	//DEBUG_PRINT("Running writeScreenBufferFade.");
	ledDriver.clear();
	uint8_t brightnessBuffer[10][12] = {};

	for (uint8_t y = 0; y <= 9; y++)
	{
		for (uint8_t x = 0; x <= 11; x++)
		{
			if (bitRead(screenBufferOld[y], 15 - x))
			{
				brightnessBuffer[y][x] = brightness;
			}
		}
	}
	for (uint8_t i = 0; i < brightness; i++)
	{
		for (uint8_t y = 0; y <= 9; y++)
		{
			for (uint8_t x = 0; x <= 11; x++)
			{
				if (!(bitRead(screenBufferOld[y], 15 - x)) && (bitRead(screenBufferNew[y], 15 - x)))
				{
					brightnessBuffer[y][x]++;
				}
				if ((bitRead(screenBufferOld[y], 15 - x)) && !(bitRead(screenBufferNew[y], 15 - x)))
				{
					brightnessBuffer[y][x]--;
				}
				ledDriver.setPixel(x, y, color, brightnessBuffer[y][x]);
			}
		}

		// Corner LEDs.
		for (uint8_t y = 0; y <= 3; y++)
		{
			ledDriver.setPixel(110 + y, color, brightnessBuffer[y][11]);
		}

#ifdef BUZZER
		// Alarm LED.
#ifdef ALARM_LED_COLOR
#ifdef ABUSE_CORNER_LED_FOR_ALARM
		if (settings.getAlarm1() || settings.getAlarm2() || timerSet)
		{
			ledDriver.setPixel(111, ALARM_LED_COLOR, brightnessBuffer[4][11]);
		}
		else
		{
			ledDriver.setPixel(111, color, brightnessBuffer[1][11]);
		}
#else
		ledDriver.setPixel(114, ALARM_LED_COLOR, brightnessBuffer[4][11]);
#endif
#else
		ledDriver.setPixel(114, color, brightnessBuffer[4][11]);
#endif
#endif

		esp8266WebServer.handleClient();
		delay((255 - brightness) / 7);
		ledDriver.show();
	}
	//DEBUG_PRINTLN(" Done.");
}

#ifdef LDR
/******************************************************************************
Set brightness from LDR.
******************************************************************************/

void setBrightnessFromLdr()
{
	// Get rated brightness from LDR.
	if (millis() > (lastLdrCheck + 250))
	{
		lastLdrCheck = millis();
#ifdef LDR_IS_INVERSE
		ldrValue = 1023 - analogRead(PIN_LDR);
#else
		ldrValue = analogRead(PIN_LDR);
#endif
		//DEBUG_PRINTLN("ldrValue: " + String(ldrValue));
		if (ldrValue < minLdrValue)
		{
			minLdrValue = ldrValue;
	}
		if (ldrValue > maxLdrValue)
		{
			maxLdrValue = ldrValue;
		}
		if (settings.getUseLdr() && ((ldrValue >= (lastLdrValue + LDR_HYSTERESIS)) || (ldrValue <= (lastLdrValue - LDR_HYSTERESIS))))
		{
			lastLdrValue = ldrValue;
			if (minLdrValue != maxLdrValue) // The ESP will crash if minLdrValue and maxLdrValue are equal due to an error in map().
			{
				ratedBrightness = map(ldrValue, minLdrValue, maxLdrValue, 0, 255);
			}
			ratedBrightness = constrain(ratedBrightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
			DEBUG_PRINTLN("Brightness: " + String(ratedBrightness) + " (value: " + String(ldrValue) + ", min: " + String(minLdrValue) + ", max: " + String(maxLdrValue) + ")");
			SYSLOG("Brightness: " + String(ratedBrightness) + " (value: " + String(ldrValue) + ", min: " + String(minLdrValue) + ", max: " + String(maxLdrValue) + ")");
		}
}

	// Set brightness to rated brightness.
	if ((millis() > (lastBrightnessSet + 50)) && settings.getUseLdr())
	{
		lastBrightnessSet = millis();
		if (brightness < ratedBrightness)
		{
			brightness++;
		}
		if (brightness > ratedBrightness)
		{
			brightness--;
		}
		if (brightness != ratedBrightness)
		{
			writeScreenBuffer(matrix, settings.getColor(), brightness);
			//DEBUG_PRINTLN("brightness: " + String(brightness) + ", ratedBrightness: " + String(ratedBrightness));
		}
	}
}
#endif

#if defined(UPDATE_INFO_STABLE) || defined(UPDATE_INFO_UNSTABLE)
/******************************************************************************
Get update info.
******************************************************************************/

void getUpdateInfo()
{
	DEBUG_PRINT("Sending HTTP-request for update info. Status: ");
	SYSLOG("Sending HTTP-request for update info.");
	if (WiFi.status() == WL_CONNECTED)
	{
		char server[] = UPDATE_INFOSERVER;
		WiFiClient wifiClient;
		RestClient restClient = RestClient(wifiClient, server, 80);
		uint16_t statusCode = restClient.get(UPDATE_INFOFILE);
		if (statusCode != 200)
		{
			DEBUG_PRINTLN(String(statusCode));
			SYSLOG("Status: " + String(statusCode));
			return;
		}
		String response = restClient.readResponse();
		DEBUG_PRINTLN(String(statusCode) + "\r\nResponse is " + String(response.length()) + " bytes.");
		SYSLOG("Status: " + String(statusCode));
		SYSLOG("Response is " + String(response.length()) + " bytes.");
		//DEBUG_PRINTLN(String(response));
		DEBUG_PRINT("Parsing JSON. ");
		SYSLOG("Parsing JSON.");
		//DynamicJsonBuffer jsonBuffer;
		StaticJsonBuffer<256> jsonBuffer;
		JsonObject &responseJson = jsonBuffer.parseObject(response);
		if (!responseJson.success())
		{
			DEBUG_PRINTLN("ERROR.");
			SYSLOG("ERROR.");
			return;
		}
		else
		{
			DEBUG_PRINTLN("OK.");
			SYSLOG("OK.");
		}
#ifdef UPDATE_INFO_STABLE
		updateInfo = responseJson["channel"]["stable"]["version"].as<String>();
#endif
#ifdef UPDATE_INFO_UNSTABLE
		updateInfo = responseJson["channel"]["unstable"]["version"].as<String>();
#endif
		DEBUG_PRINTLN("Version on server: " + updateInfo);
		SYSLOG("Version on server: " + updateInfo);
#if defined(DEBUG) || defined(SYSLOG_SERVER)
		if (updateInfo > String(FIRMWARE_VERSION))
		{
			DEBUG_PRINTLN("Firmwareupdate available!");
			SYSLOG("Firmwareupdate available!");
		}
#endif
		return;
	}
	DEBUG_PRINTLN("ERROR. No WiFi.");
	SYSLOG("ERROR. No WiFi.");
	return;
}
#endif

/******************************************************************************
Get weather conditions.
******************************************************************************/

void getWeather(String location)
{
	DEBUG_PRINT("Sending HTTP-request for weather. Status: ");
	SYSLOG("Sending HTTP-request for weather.");
	if (WiFi.status() == WL_CONNECTED)
	{
		location.replace(" ", "%20");
		location.replace(",", "%2C");
		char server[] = "query.yahooapis.com";
		WiFiClient wifiClient;
		RestClient restClient = RestClient(wifiClient, server, 80);
		String sqlQuery = "select%20atmosphere.humidity%2C%20item.title%2C%20item.condition.temp%2C%20item.condition.code%20";
		sqlQuery += "from%20weather.forecast%20where%20woeid%20in%20";
		sqlQuery += "(select%20woeid%20from%20geo.places(1)%20where%20text=%22" + location + "%22)%20";
		sqlQuery += "and%20u=%27c%27";
		uint16_t statusCode = restClient.get("query.yahooapis.com/v1/public/yql?q=" + sqlQuery + "&format=json");
		if (statusCode != 200)
		{
			DEBUG_PRINTLN(String(statusCode));
			SYSLOG("Status: " + String(statusCode));
			yahooTitle = "Request failed.";
			return;
		}
		String response = restClient.readResponse();
		response = response.substring(response.indexOf('{'), response.lastIndexOf('}') + 1);
		DEBUG_PRINTLN(String(statusCode) + "\r\nResponse is " + String(response.length()) + " bytes.");
		SYSLOG("Status: " + String(statusCode));
		SYSLOG("Response is " + String(response.length()) + " bytes.");
		//DEBUG_PRINTLN(String(response));
		DEBUG_PRINT("Parsing JSON. ");
		SYSLOG("Parsing JSON.");
		//DynamicJsonBuffer jsonBuffer;
		StaticJsonBuffer<512> jsonBuffer;
		JsonObject &responseJson = jsonBuffer.parseObject(response);
		if (!responseJson.success())
		{
			DEBUG_PRINTLN("ERROR.");
			SYSLOG("ERROR.");
			yahooTitle = "Parsing failed.";
			return;
		}
		else
		{
			DEBUG_PRINTLN("OK.");
			SYSLOG("OK.");

		}
		yahooTitle = responseJson["query"]["results"]["channel"]["item"]["title"].as<String>();
		yahooTitle = yahooTitle.substring(0, yahooTitle.indexOf(" at "));
		DEBUG_PRINTLN(yahooTitle);
		SYSLOG(yahooTitle);
		yahooTemperature = responseJson["query"]["results"]["channel"]["item"]["condition"]["temp"].as<int8_t>();
		DEBUG_PRINTLN("Temperature: " + String(yahooTemperature));
		SYSLOG("Temperature: " + String(yahooTemperature));
		yahooHumidity = responseJson["query"]["results"]["channel"]["atmosphere"]["humidity"].as<uint8_t>();
		DEBUG_PRINTLN("Humidity: " + String(yahooHumidity));
		SYSLOG("Humidity: " + String(yahooHumidity));
		yahooCode = responseJson["query"]["results"]["channel"]["item"]["condition"]["code"].as<uint8_t>();
		DEBUG_PRINTLN("Condition: " + String(yahooCode));
		SYSLOG("Condition: " + String(yahooCode));
		return;
	}
	DEBUG_PRINTLN("ERROR. No WiFi.");
	SYSLOG("ERROR. No WiFi.");
	yahooTitle = "Request failed. No WiFi.";
	return;
}

/******************************************************************************
Get time from NTP.
******************************************************************************/

void getNtpTime()
{
	DEBUG_PRINT("Sending NTP-request to \"" + String(NTP_SERVER) + "\". ");
	SYSLOG("Sending NTP-request to \"" + String(NTP_SERVER) + "\". ");
	if (WiFi.status() == WL_CONNECTED)
	{
		uint8_t packetBuffer[49] = { };
		packetBuffer[0] = 0xE3;
		packetBuffer[1] = 0x00;
		packetBuffer[2] = 0x06;
		packetBuffer[3] = 0xEC;
		packetBuffer[12] = 0x31;
		packetBuffer[13] = 0x4E;
		packetBuffer[14] = 0x31;
		packetBuffer[15] = 0x34;
		WiFiUDP wifiUdp;
		wifiUdp.begin(2390);
		char server[] = NTP_SERVER;
		IPAddress timeServerIP;
		WiFi.hostByName(server, timeServerIP);
		wifiUdp.beginPacket(timeServerIP, 123);
		wifiUdp.write(packetBuffer, 48);
		wifiUdp.endPacket();
		uint32_t beginWait = millis();
		while (millis() - beginWait < 1500)
		{
			if (wifiUdp.parsePacket() >= 48)
			{
				wifiUdp.read(packetBuffer, 48);
				uint32_t ntpTime = (packetBuffer[40] << 24) + (packetBuffer[41] << 16) + (packetBuffer[42] << 8) + packetBuffer[43];
				// NTP time is seconds from 1900, we need time from 1970.
				ntpTime -= 2208988800;
				// Shift UTC time to local time. 
				ntpTime = timeZone.toLocal(ntpTime);
				DEBUG_PRINTLN("OK.");
				SYSLOG("OK.");
#ifdef DEBUG
				debug.debugTime("Time (NTP):", ntpTime, SHOW_DATE);
#endif
#ifdef SYSLOG_SERVER
				syslog.logf(LOG_INFO, "Time (NTP): %02d:%02d:%02d %02d.%02d.%04d", hour(ntpTime), minute(ntpTime), second(ntpTime), day(ntpTime), month(ntpTime), year(ntpTime));
#endif
#ifdef RTC_BACKUP
				DEBUG_PRINTLN("*** RTC set from NTP. ***");
				SYSLOG("*** RTC set from NTP. ***");
				DEBUG_PRINTLN("Drift (RTC/NTP): " + String(ntpTime - RTC.get()) + " sec.");
				SYSLOG("Drift (RTC/NTP): " + String(ntpTime - RTC.get()) + " sec.");
				RTC.set(ntpTime);
#endif
				DEBUG_PRINTLN("*** ESP set from NTP. ***");
				SYSLOG("*** ESP set from NTP. ***");
				DEBUG_PRINTLN("Drift (ESP/NTP): " + String(ntpTime - now()) + " sec.");
				SYSLOG("Drift (ESP/NTP): " + String(ntpTime - now()) + " sec.");
				setTime(ntpTime);
				return;
			}
		}
		DEBUG_PRINTLN("ERROR. Timeout.");
		SYSLOG("ERROR. Timeout.");
		return;
	}
	DEBUG_PRINTLN("ERROR. No WiFi.");
	SYSLOG("ERROR. No WiFi.");
	return;
}

/******************************************************************************
Show feedtext.
******************************************************************************/

void showFeed(String feedText, eColor color)
{
	DEBUG_PRINT("Feed: " + feedText);
	SYSLOG("Feed: " + feedText);
	feedText = "  " + feedText + "  ";
	for (uint8_t x = 0; x < (feedText.length() - 2); x++)
	{
		for (uint8_t y = 0; y < 6; y++)
		{
			renderer.clearScreenBuffer(matrix);
			for (uint8_t z = 0; z <= 6; z++)
			{
				matrix[2 + z] |= (lettersBig[feedText[x] - 32][z] << 11 + y) & 0b1111111111100000;
				matrix[2 + z] |= (lettersBig[feedText[x + 1] - 32][z] << 5 + y) & 0b1111111111100000;
				matrix[2 + z] |= (lettersBig[feedText[x + 2] - 32][z] << y - 1) & 0b1111111111100000;
			}
			writeScreenBuffer(matrix, color, brightness);
			delay(150);
		}
		esp8266WebServer.handleClient();
	}
	DEBUG_PRINTLN(" Done.");
	SYSLOG("Done.");
}

#ifdef SENSOR_DHT22
/******************************************************************************
Get temperature or humidity from DHT22.
******************************************************************************/

float getDHT22Temperature()
{
	dhtTemperature = dht.readTemperature();
	if (!isnan(dhtTemperature))
	{
		return dhtTemperature;
	}
	return 0;
}

float getDHT22Humidity()
{
	dhtHumidity = dht.readHumidity();
	if (!isnan(dhtHumidity))
	{
		return dhtHumidity;
	}
	return 0;
}
#endif

/******************************************************************************
Misc.
******************************************************************************/

// Set mode of display.
void setMode(Mode newMode)
{
	DEBUG_PRINTLN("Mode: " + String(newMode));
	screenBufferNeedsUpdate = true;
	runTransitionOnce = false;
	lastMode = mode;
	mode = newMode;

	// Set modeTimeout.
	switch (mode)
	{
	case STD_MODE_AMPM:
	case STD_MODE_SECONDS:
	case STD_MODE_WEEKDAY:
	case STD_MODE_DATE:
#if defined(RTC_BACKUP) && !defined(SENSOR_DHT22)
	case STD_MODE_TEMP:
#endif
#ifdef SENSOR_DHT22
	case STD_MODE_TEMP:
	case STD_MODE_HUMIDITY:
#endif
	case STD_MODE_EXT_TEMP:
	case STD_MODE_EXT_HUMIDITY:
		modeTimeout = millis();
		break;
	default:
		modeTimeout = 0;
		break;
	}
}

// Switch LEDs off.
void setLedsOff()
{
	DEBUG_PRINTLN("LEDs off.");
	SYSLOG("LEDs off.");
	setMode(STD_MODE_BLANK);
}

// Switch LEDs on.
void setLedsOn()
{
	DEBUG_PRINTLN("LEDs on.");
	SYSLOG("LEDs on.");
	setMode(lastMode);
}

// Toggle LEDs.
void setDisplayToToggle()
{
	if (mode == STD_MODE_BLANK)
	{
		setLedsOn();
	}
	else
	{
		setLedsOff();
	}
}

/******************************************************************************
Webserver.
******************************************************************************/

void setupWebServer()
{
	esp8266WebServer.onNotFound(handleNotFound);
	esp8266WebServer.on("/", handleRoot);
	esp8266WebServer.on("/handle_BUTTON_ONOFF", handle_BUTTON_ONOFF);
	esp8266WebServer.on("/handle_BUTTON_TIME", handle_BUTTON_TIME);
	esp8266WebServer.on("/handle_BUTTON_MODE", handle_BUTTON_MODE);
	esp8266WebServer.on("/handle_BUTTON_SETTINGS", handle_BUTTON_SETTINGS);
	esp8266WebServer.on("/handle_BUTTON_PLUS", handle_BUTTON_PLUS);
	esp8266WebServer.on("/handle_BUTTON_MINUS", handle_BUTTON_MINUS);
	esp8266WebServer.begin();
}

// Page 404.
void handleNotFound()
{
	esp8266WebServer.send(404, "text/plain", "404 - File Not Found.");
}

// Page /.
void handleRoot()
{
	String message = "<!doctype html>";
	message += "<html>";
	message += "<head>";
	message += "<title>" + String(HOSTNAME) + "</title>";
	message += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
	message += "<meta http-equiv=\"refresh\" content=\"60\">";
	message += "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css\">";
	message += "<style>";
	message += "body {background-color: #FFFFFF; text-align: center; color: #333333; font-family: Sans-serif; font-size: 16px;}";
	message += "button {background-color: #1FA3EC; text-align: center; color: #FFFFFF; width: 200px; padding: 10px 30px; border: 5px solid #FFFFFF; display: inline-block;}";
	message += "</style>";
	message += "</head>";
	message += "<body>";
	message += "<h1>" + String(HOSTNAME) + "</h1>";
#ifdef DEDICATION
	message += DEDICATION;
	message += "<br><br>";
#endif
	if (mode == STD_MODE_BLANK)
		message += "<button onclick=\"window.location.href='/handle_BUTTON_ONOFF'\"><i class=\"fa fa-toggle-off\" style=\"font-size:24px\"></i></button>";
	else
		message += "<button onclick=\"window.location.href='/handle_BUTTON_ONOFF'\"><i class=\"fa fa-toggle-on\" style=\"font-size:24px\"></i></button>";
	message += "<button onclick=\"window.location.href='/handle_BUTTON_TIME'\"><i class=\"fa fa-clock-o\" style=\"font-size:24px\"></i></button>";
	message += "<br><br>";
	message += "<button onclick=\"window.location.href='/handle_BUTTON_MODE'\"><i class=\"fa fa-bars\" style=\"font-size:24px\"></i></button>";
	message += "<button onclick=\"window.location.href='/handle_BUTTON_SETTINGS'\"><i class=\"fa fa-gear\" style=\"font-size:24px\"></i></button>";
	message += "<br><br>";
	message += "<button onclick=\"window.location.href='/handle_BUTTON_PLUS'\"><i class=\"fa fa-plus-circle\" style=\"font-size:24px\"></i></button>";
	message += "<button onclick=\"window.location.href='/handle_BUTTON_MINUS'\"><i class=\"fa fa-minus-circle\" style=\"font-size:24px\"></i></button>";
#if defined(RTC_BACKUP) && !defined(SENSOR_DHT22)
	message += "<br><br><i class = \"fa fa-thermometer-three-quarters\" style=\"font-size:20px\"></i> " + String(RTC.temperature() / 4.0) + " &#176;C / " + String((RTC.temperature() / 4.0) * 9.0 / 5.0 + 32.0) + " &#176;F";
#endif
#ifdef SENSOR_DHT22
	message += "<br><br><i class = \"fa fa-thermometer-three-quarters\" style=\"font-size:20px\"></i> " + String(getDHT22Temperature()) + " &#176;C / " + String(getDHT22Temperature() * 9.0 / 5.0 + 32.0) + " &#176;F";
	message += "<br><i class = \"fa fa-tint\" style=\"font-size:20px\"></i> " + String(getDHT22Humidity()) + " %RH";
#endif
	message += "<br><br><font size=2><a href=\"http://tmw-it.ch/qlockwork/\">Qlockwork</a> was <i class=\"fa fa-code\" style=\"font-size:15px\"></i> with <i class=\"fa fa-heart\" style=\"font-size:13px\"></i> by ch570512.";
	message += "<br>Firmware: " + String(FIRMWARE_VERSION);
#if defined(UPDATE_INFO_STABLE) || defined(UPDATE_INFO_UNSTABLE)
	if (updateInfo > String(FIRMWARE_VERSION))
		message += "<br><span style=\"color: red;\">Firmwareupdate available! (" + updateInfo + ")</span>";
#endif
#ifdef DEBUG_WEBSITE
	message += "<br><br>Time: " + String(hour()) + ":";
	if (minute() < 10)
		message += "0" + String(minute());
	else
		message += String(minute());
	message += "<br>Date: " + String(dayStr(weekday())) + ", " + String(monthStr(month())) + " " + String(day()) + ". " + String(year());
	message += "<br>Free RAM: " + String(system_get_free_heap_size()) + " bytes";
	message += "<br>LED-Driver: " + ledDriver.getSignature();
#ifdef LDR
	message += "<br>Brightness: " + String(brightness) + " (LDR: ";
	if (settings.getUseLdr())
		message += "enabled";
	else
		message += "disabled";
	message += ", value: " + String(ldrValue) + ", min: " + String(minLdrValue) + ", max : " + String(maxLdrValue) + ")";
#endif
	message += "<br>Weather: " + String(yahooTitle);
#endif
	message += "</font>";
	message += "</body>";
	message += "</html>";
	esp8266WebServer.send(200, "text/html", message);
}

// Site buttons.

void handle_BUTTON_ONOFF()
{
	String message = "<!doctype html><html><head><script>window.onload  = function() {window.location.replace('/')};</script></head><body></body></html>";
	esp8266WebServer.send(200, "text/html", message);
	buttonOnOffPressed();
}

void handle_BUTTON_TIME()
{
	String message = "<!doctype html><html><head><script>window.onload  = function() {window.location.replace('/')};</script></head><body></body></html>";
	esp8266WebServer.send(200, "text/html", message);
	buttonTimePressed();
}

void handle_BUTTON_MODE()
{
	String message = "<!doctype html><html><head><script>window.onload = function() {window.location.replace('/')};</script></head><body></body></html>";
	esp8266WebServer.send(200, "text/html", message);
	buttonModePressed();
}

void handle_BUTTON_SETTINGS()
{
	String message = "<!doctype html><html><head><script>window.onload = function() {window.location.replace('/')};</script></head><body></body></html>";
	esp8266WebServer.send(200, "text/html", message);
	buttonSettingsPressed();
}

void handle_BUTTON_PLUS()
{
	String message = "<!doctype html><html><head><script>window.onload  = function() {window.location.replace('/')};</script></head><body></body></html>";
	esp8266WebServer.send(200, "text/html", message);
	buttonPlusPressed();
}

void handle_BUTTON_MINUS()
{
	String message = "<!doctype html><html><head><script>window.onload  = function() {window.location.replace('/')};</script></head><body></body></html>";
	esp8266WebServer.send(200, "text/html", message);
	buttonMinusPressed();
}
