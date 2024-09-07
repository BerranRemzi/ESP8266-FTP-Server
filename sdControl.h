#ifndef SD_CONTROL_H
#define SD_CONTROL_H

#define SPI_BLOCKOUT_PERIOD_SECONDS 20u
#define SPI_BLOCKOUT_PERIOD_MS (SPI_BLOCKOUT_PERIOD_SECONDS * 1000u)

class SDControl
{
public:
  SDControl() {}
  static void setup(uint8_t pin);
  static bool takeBusControl();
  static bool releaseBusControl();
  static volatile uint32_t _spiBlockoutTime;
  static bool _weTookBus;

private:
  static uint8_t _csPin;
};

extern SDControl sdControl;

#endif
