#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <assert.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
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

#include "Common/Tools/btc.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

#define MAX_DEVICES 8
#define CS_PIN GPIO_NUM_5
#define ONBOARD_LED GPIO_NUM_2

namespace {

struct alignas(uint32_t) my_rtc_data_t
{
    bool flip;
};

const struct ha_connect_t {
    const String url = F("https://" HA_ADDRESS ":" HA_PORT "/api/states/");
    // Mix token and magic word
    const String token = F("Bearer " HA_TOKEN);
} ha_connect;

RTC_DATA_ATTR CoolESP::RTCWifi<my_rtc_data_t> rtc_wifi;

const std::array<const String, 2> sensors = {
    F(HA_SENSOR1),
    F(HA_SENSOR2)
};

constexpr uint16_t ha_http_timeout_ms = 5000;

// All HA requests of one wake-up share a single TLS connection. Opening a
// new one for every request means a full TLS handshake each time, which
// keeps the ESP awake (and draining the battery) for no good reason.
// Both objects have to live as long as the connection does, because the
// destructor of HTTPClient closes it.
struct ha_session_t {
    WiFiClientSecure client;
    HTTPClient http;

    ha_session_t()
    {
        client.setInsecure();
        http.setReuse(true);
        http.setTimeout(ha_http_timeout_ms);
    }
};

ha_session_t &ha_session()
{
    static ha_session_t session;
    return session;
}

std::optional<String> ha_get_sensor(const String &sensor)
{
    ha_session_t &session = ha_session();
    HTTPClient &http = session.http;

    // The server may have closed the kept-alive connection in the meantime.
    // Such a request fails at the transport level (negative return code),
    // so retry it once on a fresh connection.
    for (int attempt = 0; attempt < 2; ++attempt) {
        http.begin(session.client, ha_connect.url + sensor);
        http.addHeader(F("Authorization"), ha_connect.token);
        http.addHeader(F("content-type"), F("application/json"));

        const int response_code = http.GET();
        if (response_code == 200) {
            String payload = http.getString();
            http.end();
            return payload;
        }

        http.end();

        if (response_code > 0) {
            // The server answered, just not with what we need.
            // Retrying would not help.
            return std::nullopt;
        }

        session.client.stop();
    }

    return std::nullopt;
}

std::optional<String> ha_get_state_from(const String &sensor)
{
    const auto payload = ha_get_sensor(sensor);
    if (!payload) {
        Serial.println(F("Unable to get payload from HA."));
        return std::nullopt;
    }

    // Only "state" is needed. The filter drops everything else (attributes,
    // timestamps, ...) while parsing, so a small document is always enough
    // and a sensor with many attributes can not run out of memory.
    DynamicJsonDocument filter(32);
    filter["state"] = true;

    DynamicJsonDocument doc(128);
    const DeserializationError error =
        deserializeJson(doc, *payload, DeserializationOption::Filter(filter));
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return std::nullopt;
    }

    // A missing "state" must not be turned into the text "null".
    const char *state = doc["state"];
    if (!state) {
        Serial.println(F("No state in HA response."));
        return std::nullopt;
    }

    return String(state);
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

        ESP.restart();
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

    
    ArduinoOTA.setHostname((const char *) Config::Wifi::mdns_hostname);
    ArduinoOTA.setPassword((const char *) Config::Wifi::ota_password);
    ArduinoOTA.begin();
}


void populate_matrix(MD_Parola &matrix, const String &out)
{
    matrix.displayClear();
    matrix.print(out);
}

// ---------- FETCH: only HTTP requests, the matrix is not touched ----------

// HA reports "unknown" or "unavailable" instead of a real value, for example
// when a sensor is offline. Such a state must never be shown or parsed
// (String::toInt() returns 0 for any text, which would pass as midnight).
bool is_valid_state(const String &state)
{
    return state.length() > 0 && state != "unknown" && state != "unavailable";
}

String fetch_btc_text()
{
    const auto btc_price = btc::get_usd_price();
    if (!btc_price) {
        Serial.println(F("Unable to get BTC price."));
        return "BTC error";
    }

    return *btc_price;
}

String fetch_sensor_text()
{
    String out;
    out.reserve(16);

    for (const auto &sensor : sensors)
    {
        const auto temp = ha_get_state_from(sensor);
        if (!temp || !is_valid_state(*temp)) {
            Serial.println(F("Unable to get temperature from HA."));
            return "HA issue";
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

    return out;
}

#ifdef HA_HOUR_SENSOR
std::optional<long> fetch_hour()
{
    const auto hour_ret = ha_get_state_from(HA_HOUR_SENSOR);
    if (!hour_ret || !is_valid_state(*hour_ret)) {
        Serial.println("HA issue: get hour.");
        return std::nullopt;
    }

    const long hour = (*hour_ret).toInt();

    // Check input
    if (!(hour >= 0 && hour <= 23)) {
        Serial.println("HA value Hour: parse error");
        return std::nullopt;
    }

    return hour;
}

// ---------- DRAW: no HTTP requests, only drawing into the matrix buffer ----------

void draw_hour(MD_MAX72XX *const matrix_hw, long hour)
{
    const long led_index = (hour > 12) ? 14 : 0;

    // Set AM/PM mark
    matrix_hw->setPoint(7, 12, hour <= 12);
    matrix_hw->setPoint(7, 13, hour <= 12);

    // Set ending
    matrix_hw->setPoint(7, 26, true);
    matrix_hw->setPoint(7, 27, true);

    if (hour > 12) {
        // Set all 12 LEDs
        matrix_hw->setRow(0, 0, 7, 0xFF);
        matrix_hw->setRow(1, 1, 7, 0b00001111);
        hour = hour - 12;
    }

    for (long p = 0; p < hour; ++p) {
        matrix_hw->setPoint(7, p + led_index, true);
    }
}
#endif

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
    const bool handle_ota = !rtc_wifi.is_restored();

    Serial.begin(115200);

    // Disable blue crazy led
    pinMode(ONBOARD_LED, OUTPUT);
    digitalWrite(ONBOARD_LED, HIGH);

    // Don't mess with matrix after deep sleep
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    SPI1.setClockDivider(SPI_CLOCK_DIV128);
    SPI1.begin();

    if (!matrix.begin()) {
        Serial.println("Matrix failed");
        return;
    }

    matrix.setInvert(false);
    matrix.setIntensity(0);
    matrix.displayClear();
    matrix.setTextAlignment(PA_CENTER);

    const esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP)
    {
        populate_matrix(matrix, "Hello!");
        delay(1000);
    }

    rtc_wifi.connect(Config::Wifi::ssid, Config::Wifi::password);

    if (handle_ota)
    {
        Serial.println(F("First start. Handling ota update."));
        populate_matrix(matrix, "OTA wait");
        setup_OTA();

        MDNS.begin(Config::Wifi::mdns_hostname);

        delay_20s.start(20000, AsyncDelay::MILLIS);
        do {
            ArduinoOTA.handle();
        } while(!delay_20s.isExpired());

        ArduinoOTA.end();
    }

    const auto night_mode = ha_get_state_from(HA_NIGHT_MODE);
    if (night_mode && *night_mode == "on") {
        // Sleep for 10 minutes
        matrix.displayShutdown(true);
        CoolESP::Utils::sleep_me(600000000UL);

        // No return from previous call but ...
        return;
    }

    // Get all data first. The matrix is not touched during HTTP requests.
    auto &data = rtc_wifi.get_rtc_data().get();
    const String text = data.flip ? fetch_btc_text() : fetch_sensor_text();
    data.flip = !data.flip;

#ifdef HA_HOUR_SENSOR
    const auto hour = fetch_hour();
#endif

    // Prepare matrix and draw everything at once
    MD_MAX72XX *const matrix_hw = matrix.getGraphicObject();
    matrix_hw->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
    populate_matrix(matrix, text);

    // print() may switch UPDATE back on, so turn it off again
    matrix_hw->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

#ifdef HA_HOUR_SENSOR
    if (hour) {
        draw_hour(matrix_hw, *hour);
    }
#endif

    // update matrix
    matrix_hw->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
    matrix_hw->update();

    // sleep for 5 minutes
    CoolESP::Utils::sleep_me(300000000UL);
}

void loop() {
    // We should never reach this code so make it die.
    assert(false);
    abort();
}