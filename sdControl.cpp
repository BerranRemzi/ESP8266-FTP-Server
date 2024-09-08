#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile uint32_t SDControl::_spiBlockoutTime = 0;
bool SDControl::_weTookBus = false;
uint8_t SDControl::_csPin = NOT_A_PIN;

IRAM_ATTR void onBusActivitylInterrupt()
{
	if (!sdControl._weTookBus)
	{
		sdControl._spiBlockoutTime = millis() + SPI_BLOCKOUT_PERIOD_MS;
	}
}

void SDControl::setup(uint8_t csPin)
{
	_csPin = csPin;
	releaseBusControl();
	attachInterrupt(digitalPinToInterrupt(_csPin), &onBusActivitylInterrupt, FALLING);
}

bool SDControl::takeBusControl()
{
	bool returnValue = false;
	if (_spiBlockoutTime < millis())
	{
		_weTookBus = true;
		// LED_ON;
		pinMode(MISO_PIN, SPECIAL);
		pinMode(MOSI_PIN, SPECIAL);
		pinMode(SCLK_PIN, SPECIAL);
		pinMode(_csPin, OUTPUT);

		returnValue = true;
	}

	return returnValue;
}

bool SDControl::releaseBusControl()
{
	pinMode(MISO_PIN, INPUT);
	pinMode(MOSI_PIN, INPUT);
	pinMode(SCLK_PIN, INPUT);
	pinMode(_csPin, INPUT);
	// LED_OFF;
	_weTookBus = false;

	return true;
}
