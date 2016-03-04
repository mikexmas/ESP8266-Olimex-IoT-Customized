/*
 Driver for the temperature and humidity sensor DHT11 and DHT22
 Official repository: https://github.com/CHERTS/esp8266-dht11_22

 Copyright (C) 2014 Mikhail Grigorev (CHERTS)

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along
 with this program; if not, write to the Free Software Foundation, Inc.,
 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "dhtxx/dhtxx.h"
#include "easygpio/easygpio.h"

#define DHT_MAXTIMINGS  10000
#define DHT_BREAKTIME 20
#define DHT_MAXCOUNT  32000

#define sleepms(x) os_delay_us((x)*1000);

#ifdef DHT_DEBUG
#undef DHT_DEBUG
#define DHT_DEBUG(...) os_printf(__VA_ARGS__);
#else
#define DHT_DEBUG(...)
#endif

static inline float 
scale_humidity(DHT_Type sensor_type, int *data) {
  if (sensor_type == DHT11) {
    return (float) data[0];
  } else {
    float humidity = data[0] * 256 + data[1];
    return humidity / 10.0;
  }
}

static inline float 
scale_temperature(DHT_Type sensor_type, int *data) {
  if (sensor_type == DHT11) {
    return (float) data[2];
  } else {
    float temperature = data[2] & 0x7f;
    temperature *= 256;
    temperature += data[3];
    temperature /= 10.0;
    if (data[2] & 0x80)
      temperature *= -1.0;
    return temperature;
  }
}

/**
 * read the sensor (blocking function)
 * returns false if it fails to read the sensor
 */
bool ICACHE_FLASH_ATTR
dht_read(DHT_Sensor *sensor, DHT_Sensor_Output* output) {
  uint8_t pin = sensor->pin;

  int counter = 0;
  int laststate = 1;
  int i = 0;
  int j = 0;
  int checksum = 0;
  int data[100];
  data[0] = data[1] = data[2] = data[3] = data[4] = 0;

  // Wake up device, 250ms of high
  easygpio_outputEnable(pin, 1);
  sleepms(250);
  // Hold low for 20ms
  easygpio_outputSet(pin, 0);
  sleepms(20);
  // High for 40ns
  easygpio_outputSet(pin, 1);
  os_delay_us(40);
  // Set DHT_PIN pin as an input
  easygpio_outputDisable(pin);

  // wait for pin to drop?
  while (easygpio_inputGet(pin) && i < DHT_MAXCOUNT) {
    os_delay_us(1);
    i++;
  }

  if (i == DHT_MAXCOUNT) {
    DHT_DEBUG("dht_read:Failed to get reading from GPIO%d, dying\n", pin);
    return false;
  }

  // read data
  for (i = 0; i < DHT_MAXTIMINGS; i++) {
    // Count high time (in approx us)
    counter = 0;
    while (easygpio_inputGet(pin) == laststate) {
      counter++;
      os_delay_us(1);
      if (counter == 1000)
        break;
    }
    laststate = easygpio_inputGet(pin);
    if (counter == 1000)
      break;
    // store data after 3 reads
    if ((i > 3) && (i % 2 == 0)) {
      // shove each bit into the storage bytes
      data[j / 8] <<= 1;
      if (counter > DHT_BREAKTIME)
        data[j / 8] |= 1;
      j++;
    }
  }

  if (j >= 39) {
    checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    DHT_DEBUG("DHT%s: %02x %02x %02x %02x [%02x] CS: %02x (GPIO%d)\n",
              sensor->type==DHT11?"11":"22",
              data[0], data[1], data[2], data[3], data[4], checksum, pin);
    if (data[4] == checksum) {
      // checksum is valid
      output->temperature = scale_temperature(sensor->type, data);
      output->humidity = scale_humidity(sensor->type, data);
      DHT_DEBUG("Temperature*100 =  %d *C, Humidity*100 = %d %% (GPIO%d)\n",
          (int) (output->temperature * 100), (int) (output->humidity * 100), pin);
      return true;
    } else {
      DHT_DEBUG("dht_read: Checksum was incorrect after %d bits. Expected %d but got %d (GPIO%d)\n",
                j, data[4], checksum, pin);
      return false;
    }
  } else {
    DHT_DEBUG("dht_read: Got too few bits: %d should be at least 40 (GPIO%d)\n", j, pin);
    return false;
  }
  return true;
}

/**
 * Quick and dirty sprintf(buffer, "%.2f", value) (two decimals).
 * It is not 100% accurate with the last decimal due to rounding errors,
 * but it seems to be off by at most +-0.01.
 * You better be sure that your buffer is large enough to hold the produced string.
 */
char* ICACHE_FLASH_ATTR
dht_float2String(char* buffer, float value) {
  os_sprintf(buffer, "%d.%d", (int)(value),(int)((value - (int)value)*100));
  return buffer;
}

/**
 * Initializes the sensor, sets up the GPIO as output.
 * returns false if it fails (e.g if you use bad pin numbers)
 */
bool ICACHE_FLASH_ATTR
dht_init(DHT_Sensor *sensor, DHT_Type dht_type, uint8_t pin) {
  sensor->type = dht_type;
  sensor->pin = pin;
  bool rv = easygpio_pinMode(pin, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
  if (rv) {
    os_printf("DHT setup for type %s pin=%d\n", dht_type==DHT11?"DHT11":"DHT22", pin);
  } else {
    os_printf("Failed to setup DHT for type %s pin=%d\n", dht_type==DHT11?"DHT11":"DHT22", pin);
  }
  return rv;
}

