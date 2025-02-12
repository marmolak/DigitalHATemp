#pragma once

#include <Arduino.h>

namespace btc {
    const String url = "https://api.gemini.com/v1/pubticker/btcusd";

    std::optional<String> get_usd_price();
};