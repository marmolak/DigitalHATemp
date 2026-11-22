#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "Common/Tools/btc.h"

namespace btc {

namespace {

constexpr uint16_t http_timeout_ms = 5000;

} // end of anonymous namespace

std::optional<String> get_usd_price()
{
    // Explicit TLS client, the same way as for HA. Without it the behaviour
    // of HTTPClient::begin(url) for https depends on the core version.
    // The client has to outlive HTTPClient, whose destructor uses it,
    // so it is declared first.
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(http_timeout_ms);

    if (!http.begin(client, btc::url)) {
        Serial.println(F("BTC: HTTP begin() failed."));
        return std::nullopt;
    }

    const int response_code = http.GET();
    if (response_code != 200) {
        Serial.printf("BTC: HTTP GET failed: %d\n", response_code);
        http.end();
        return std::nullopt;
    }

    const String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        Serial.println(F("Unable to get payload for btc."));
        return std::nullopt;
    }

    // Only "last" is needed, the filter drops the rest while parsing.
    JsonDocument filter;
    filter["last"] = true;

    JsonDocument doc;
    const DeserializationError error =
        deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (error) {
        Serial.print(F("btc deserializeJson() failed: "));
        Serial.println(error.f_str());
        return std::nullopt;
    }

    // A missing "last" must not be turned into the text "null".
    const char *last = doc["last"].as<const char *>();
    if (!last || last[0] == '\0') {
        Serial.println(F("No price in BTC response."));
        return std::nullopt;
    }

    return String(last);
}

} // end namespace
