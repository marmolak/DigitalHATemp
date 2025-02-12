#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "Common/Tools/btc.h"

namespace btc {

std::optional<String> get_usd_price()
{
    HTTPClient http;

    http.begin(btc::url);
    http.addHeader(F("content-type"), F("application/json"));

    const int response_code = http.GET();
    if (response_code != 200) {
        return std::nullopt;
    }

    String payload = http.getString();

    if (!payload) {
        Serial.println(F("Unable to get payload for btc."));
        return std::nullopt;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.print(F("btc deserializeJson() failed: "));
        Serial.println(error.f_str());
        return std::nullopt;
    }

    return doc["last"];
}

} // end namespace