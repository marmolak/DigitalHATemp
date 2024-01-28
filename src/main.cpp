#include <Arduino.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include "MD_Parola.h"
#include "MD_MAX72xx.h"
#include <SPI.h>
#include <AsyncDelay.h>
#include <ESPmDNS.h>

#include "driver/rtc_io.h"

#include <optional>
#include <array>

#include "Config/Wifi.hpp"
#include "Config/Common.hpp"

#include "Common/Arch/ESP/Utils.hpp"
#include "Common/Arch/ESP/RTCWifi.hpp"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

#define MAX_DEVICES 8
#define CS_PIN GPIO_NUM_5
#define ONBOARD_LED GPIO_NUM_2

namespace {

struct alignas(uint32_t) my_rtc_data_t
{
};

const struct ha_connect_t {
    const String url = F("http://" HA_ADDRESS ":" HA_PORT "/api/states/");
    // Mix token and magic word
    const String token = F("Bearer " HA_TOKEN);
} ha_connect;

RTC_DATA_ATTR CoolESP::RTCWifi<my_rtc_data_t> rtc_wifi;

const std::array<const String, 2> sensors = {
    F(HA_SENSOR1),
    F(HA_SENSOR2)
};

std::optional<String> ha_get_sensor(const String &sensor)
{
    HTTPClient http;

    http.begin(ha_connect.url + sensor);
    http.addHeader(F("Authorization"), ha_connect.token);
    http.addHeader(F("content-type"), F("application/json"));

    const int response_code = http.GET();
    if (response_code != 200) {
        return std::nullopt;
    }

    String payload = http.getString();
    return payload;
}

std::optional<String> ha_get_temp_from(const String &sensor)
{
    const auto payload = ha_get_sensor(sensor);
    if (!payload) {
        Serial.println(F("Unable to get payload from HA."));
        return std::nullopt;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, *payload);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return std::nullopt;
    }

    return doc["state"];
}

void setup_OTA()
{
    ArduinoOTA.onStart([]()
    {
        if (ArduinoOTA.getCommand() == U_FLASH) { return; }
        pinMode(CS_PIN, OUTPUT);
        digitalWrite(CS_PIN, HIGH);

        // Take care only about FS flash.
        LittleFS.end();
    });

    ArduinoOTA.onEnd([]()
    {
        LittleFS.begin();

        // Kind of magic. Prevent broken display content after upgrade.
        CoolESP::Utils::sleep_me(30000UL);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    ArduinoOTA.setHostname((const char *) Config::Wifi::mdns_hostname);
    ArduinoOTA.setPassword((const char *) Config::Wifi::ota_password);
}

void populate_matrix(MD_Parola &matrix, const String &out)
{
    matrix.displayClear();
    matrix.print(out);
    matrix.flush();
}

} // end of namespace

void setup()
{
    gpio_hold_dis(GPIO_NUM_23);
    gpio_hold_dis(GPIO_NUM_18);
    gpio_hold_dis(GPIO_NUM_5);
    gpio_deep_sleep_hold_dis();

    SPIClass SPI1(VSPI);
    MD_Parola matrix { MD_Parola(HARDWARE_TYPE, SPI1, CS_PIN, MAX_DEVICES) };

    AsyncDelay delay_20s;
    bool ha_error = false;
    const bool handle_ota = !rtc_wifi.is_restored();
    String out;
    out.reserve(16);

    Serial.begin(115200);

    pinMode(ONBOARD_LED, OUTPUT);
    digitalWrite(ONBOARD_LED, HIGH);

    // Don't mess with matrix
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    SPI1.begin();

    if (!matrix.begin()) {
        Serial.println("Matrix failed");
    }

    matrix.setInvert(false);
    matrix.setIntensity(0);
    matrix.displayClear();
    matrix.setTextAlignment(PA_CENTER);

    rtc_wifi.connect(Config::Wifi::ssid, Config::Wifi::password);

    if (handle_ota) [[unlikely]]
    {
        Serial.println(F("First start. Handling ota update."));
        matrix.displayClear();
        matrix.print("OTA wait");

        setup_OTA();

        MDNS.begin(Config::Wifi::mdns_hostname);

        delay_20s.start(20000, AsyncDelay::MILLIS);
        do {
            ArduinoOTA.handle();
        } while(!delay_20s.isExpired());

        ArduinoOTA.end();
    }

    for (const auto &sensor : sensors)
    {
        const auto temp = ha_get_temp_from(sensor);
        if (!temp || *temp == "unknown") {
            Serial.println(F("Unable to get temperature from HA."));
            matrix.displayClear();
            matrix.print("HA issue");
            ha_error = true;
            break;
        }
        String sensor_value = *temp;
        if (sensor_value.indexOf('.') == -1) {
            sensor_value += ".0";
        }
        out += sensor_value + " ";
    }
    out.trim();

    Serial.println("HA Temps: " + out);
    Serial.flush();

    if (!ha_error) {
        Serial.println("Update temp on matrix.");
        populate_matrix(matrix, out);
    }

    // sleep for 5 minutes
    CoolESP::Utils::sleep_me(300000000UL);
}

void loop() {
}