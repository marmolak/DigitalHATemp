#pragma once

#include <stdint.h>
#include <type_traits>

#include <ESP.h>

namespace CoolESP {

template <typename Storage>
class RTC
{
    static_assert(sizeof(Storage) <= (8 * 1024), "There is only 8 Kbytes free in RTC memory.");

    public:

        // It's your responsibility to commit data to memory...
        const Storage &get() const
        {
            return rtc_data;
        }

        Storage &get()
        {
            return rtc_data;
        }

    private:
        Storage rtc_data;
};

} // end of namespace