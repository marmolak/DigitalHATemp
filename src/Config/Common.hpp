#pragma once

// Home Assistant config
#define HA_ADDRESS  ""
#define HA_PORT     "8123"

#define HA_TOKEN    ""

// Uncomment if you want single line hour indicator
// #define HA_HOUR_SENSOR "sensor.date_and_time"

#define HA_SENSOR1 ""
#define HA_SENSOR2 ""

// You need to define input_boolean.ticker_night_mode in HA.
// More info: https://www.home-assistant.io/integrations/input_boolean/
#define HA_NIGHT_MODE "input_boolean.ticker_night_mode"
