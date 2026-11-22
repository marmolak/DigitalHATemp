# Home Assistant setup for tictoctemp

The firmware only asks Home Assistant (HA) for entity states through the REST API. It sends
nothing to HA and creates nothing in it. Every entity the firmware reads therefore has to be
created in HA by hand. This document says which ones, how, and how to verify it.

A ready-made HA package is in [`homeassistant/tictoctemp.yaml`](../homeassistant/tictoctemp.yaml).

## What the firmware reads

| Macro in the firmware config | HA entity | Expected state | If the state is invalid |
|---|---|---|---|
| `HA_SENSOR1`, `HA_SENSOR2` | your own temperature sensors (`sensor.*`) | a number, e.g. `21.5` | the display shows `HA issue` |
| `HA_NIGHT_MODE` | a boolean (`input_boolean.*`, `binary_sensor.*` or `switch.*`) | `on` = night mode | ignored, the device runs normally |
| `HA_HOUR_SENSOR` (optional) | a sensor with the hour | integer `0`-`23`, local time | the hour strip is not drawn |

Notes:

- `HA_HOUR_SENSOR` is behind an `#ifdef` in the code. Without the macro the hour strip is not
  compiled at all.
- An invalid state is an empty string, `unknown` or `unavailable`, or a response without `state`.
- A temperature is shown exactly as HA returns it. If a sensor reports too many decimals, wrap it
  in a template sensor that rounds it (`{{ states('sensor.x') | float(0) | round(1) }}`).

## Night mode (the boolean)

This is the only on/off value the firmware reads.

- The exact state `on` means night mode. The display is switched off, the device sleeps for
  10 minutes and then asks again.
- Anything else (`off`, `unavailable`, an HA outage, a malformed response) means normal operation
  with a refresh every 5 minutes. This is intentional: an HA outage must not blank the display
  by accident.
- A change takes effect only at the next wake-up: within 5 minutes when switching night mode on,
  within 10 minutes when switching it off.

## Setup steps

1. **Package.** Copy `homeassistant/tictoctemp.yaml` to `/config/packages/tictoctemp.yaml` on the
   HA machine. It creates `input_boolean.tictoctemp_night_mode`, the sensor
   `sensor.tictoctemp_hour` and two automations that switch night mode on at 23:00 and off at
   6:30. Adjust the times to taste. Requires HA 2024.10 or newer.

2. **Enable packages** in `configuration.yaml` (if you do not have them yet):

   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

3. **Check and restart.** Developer tools -> YAML -> Check configuration, then restart HA.
   A restart is required the first time a package is added.

4. **Token.** On your profile page (`/profile`) create a Long-lived access token. It is shown
   only once.

5. **Firmware config.** Add to the config headers (`Config/*.hpp`, kept out of git):

   ```cpp
   #define HA_ADDRESS     "ha.local"      // host name or IP
   #define HA_PORT        "8123"          // must be a string, not a number
   #define HA_TOKEN       "..."           // the long-lived token
   #define HA_NIGHT_MODE  "input_boolean.tictoctemp_night_mode"
   #define HA_HOUR_SENSOR "sensor.tictoctemp_hour"
   ```

   `HA_SENSOR1` and `HA_SENSOR2` are the entity IDs of your temperature sensors.

6. **HTTPS.** The firmware always calls `https://HA_ADDRESS:HA_PORT/api/states/<entity_id>`.
   If HA answers only plain HTTP, the connection fails. HTTPS has to come from HA itself
   (`http:` -> `ssl_certificate` and `ssl_key`) or from a reverse proxy. The certificate is not
   verified at the moment (`setInsecure()`), so a self-signed one works.

## Verification

The same request the firmware makes (`-k` matches `setInsecure()`):

```sh
export HA_TOKEN='...'
curl -sk -H "Authorization: Bearer $HA_TOKEN" \
  https://ha.local:8123/api/states/input_boolean.tictoctemp_night_mode
```

The response should contain `"state":"off"` or `"state":"on"`. The hour:

```sh
curl -sk -H "Authorization: Bearer $HA_TOKEN" \
  https://ha.local:8123/api/states/sensor.tictoctemp_hour
```

To switch night mode on for a test (`turn_off` switches it off again):

```sh
curl -sk -X POST -H "Authorization: Bearer $HA_TOKEN" -H "Content-Type: application/json" \
  -d '{"entity_id": "input_boolean.tictoctemp_night_mode"}' \
  https://ha.local:8123/api/services/input_boolean/turn_on
```

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `HA issue` on the display | typo in an entity ID (HA returns 404), wrong token (401), the sensor is `unknown` or `unavailable` |
| `Unable to get payload from HA.` in the serial log | the firmware did not reach HA or got a status other than 200: check HTTPS, address, port, token |
| night mode does not react | the state is not exactly `on`, wrong ID in `HA_NIGHT_MODE`, or the delay of up to 5 minutes |
| hour is off by one | the HA time zone (Settings -> System -> General), `now()` uses it |
| hour strip missing | `HA_HOUR_SENSOR` is not defined, or the sensor returns `unavailable` |

## Variant: night mode without automations

If you do not want a manual switch, only a rule based on the hour, a template binary sensor is
enough. `HA_NIGHT_MODE` is then `binary_sensor.tictoctemp_night_mode`. The downside is that it
cannot be switched by hand. The upside is that you need neither the `input_boolean` nor the
automations from the package.

```yaml
template:
  - binary_sensor:
      - name: Tictoctemp night mode
        unique_id: tictoctemp_night_mode_auto
        state: "{{ now().hour >= 23 or now().hour < 7 }}"
```

Another option is an HA Schedule helper (Settings -> Devices & services -> Helpers). Its state
is `on` or `off`, so it works with the firmware unchanged.

## Security

- The token gives full access to the API. Keep it only in a config that is not in git.
- Best create a separate non-admin HA user for the device and create the token for that user.
  The token can be revoked at any time on that user's profile, but after revoking it the
  firmware has to be flashed again with a new one.
