#include "Common/Arch/ESP/Utils.hpp"
#include <Esp.h>
#include "esp_wifi.h"

namespace CoolESP { namespace Utils {

[[noreturn]] void sleep_me(const uint64_t time_us)
{
    Serial.flush();

    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    gpio_hold_en(GPIO_NUM_23);
    gpio_hold_en(GPIO_NUM_18);
    gpio_hold_en(GPIO_NUM_5);

    // onboard led
    gpio_hold_en(GPIO_NUM_2);
    gpio_deep_sleep_hold_en();

    esp_wifi_stop();

    ESP.deepSleep(time_us);
}

}} // end of namespaces
