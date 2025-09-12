# The ESP32 Pinball RGB Animation clock

![video of the clock in action](dev/readme.gif)

Display the time and animations on HUB75 LED panels.

  * Anti aliased fonts
  * Full unicode support (emojis!)
  * Python .ttf font converter tool
  * Alpha blending
  * Background shaders
  * USB-PD powered. Can drive the two 64 x 32 panels at full brightness. The LED turns on if PD negotiation succeeded. Otherwise the clock will run in low-power mode
  * Ambient light sensor

[Video of how it looks like](https://www.youtube.com/watch?v=0dwTC5q5t4M)

[Older writeup](http://yetifrisstlama.blogspot.com/2018/02/the-esp32-pinball-rgb-matrix-animation.html)

# Building
Needs [platform.io](https://platformio.org/) installed.

To install the toolchain and build the project:

```bash
$ pio run
```

Connect USB cable and upload the SPIFFS file system:

```bash
$ pio run -t uploadfs
```

Upload the firmware and show the serial console:

```bash
$ pio run -t upload -t monitor
```

# SD card instructions
Format as FAT32, then copy the following files:
  * `./settings.json`
  * `./animations.img`
  * `./fnt/{d}.fnt`

The .fnt files can be generated from .ttf files with `dev/font_converter.py`.

## `settings.json`
If this file does not exist or cannot be parsed, a new file with default settings will be created.

Once the clock is connected to the network, `settings.json` can be edited in the web-interface at `http://espirgbani.local/`.

```json
{
    "panel_io": {
        "CLK": 13,
        "R1": 22,
        "G1": 23,
        "B1": 21,
        "R2": 18,
        "G2": 19,
        "B2": 5,
        "A": 16,
        "B": 17,
        "C": 2,
        "D": 4,
        "E": 32,
        "LAT": 15,
        "OE_N": 33
    },
    "panel": {
        "test_pattern": true,
        "tp_brightness": 10,
        "low_power_brightness": 20,
        "is_clk_inverted": true,
        "clkm_div_num": 4,
        "max_frame_rate": 30,
        "is_gamma": false,
        "is_locked": true
    },
    "delays": {
        "font": 3600,
        "color": 600,
        "ani": 30,
        "shader": 400
    },
    "power": {
        "mode": 1,
        "offset": 0,
        "divider": 400,
        "max_limit": 100,
        "min_limit": 2,
        "day": {
            "h": 9,
            "m": 0,
            "p": 20
        },
        "night": {
            "h": 22,
            "m": 45,
            "p": 2
        }
    },
    "wifis": {
        "INSERT_SSID": "INSERT_PASSWORD"
    },
    "hostname": "espirgbani",
    "log_level":    {
        "*":    3,
        "httpd_sess":   2,
        "httpd_txrx":   2,
        "httpd_ws": 2,
        "httpd_server": 2,
        "httpd_parse":  2,
        "httpd":    2,
        "spi_master":   2,
        "vfs_fat":  2,
        "wifi": 2
    },
    "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
    "ntp_host": "pool.ntp.org"
}
```

### `panel_io` section
This section allows you to customize which pins are used on the ESP32 to connect to the panel pins.
The numbers are ESP32 GPIO numbers.

For example, if you run the test-pattern and notice that the red and green colors are swapped,
you can swap their pins on the ESP32 like so:

```json
    "panel_io": {
        "R1": 23,
        "G1": 22,
        "R2": 19,
        "G2": 18
    }
```

### `panel` section
Not all LED panels are the same. Here the timing parameters of the I2S panel driver can be configured.

  * `test_pattern`: if `true`, enters a LED panel test mode instead of normal operation
  * `tp_brightness`: brightness of the test pattern, from 1 to 127. Current draw gets ridiculous for the higher values
  * `low_power_brightness`: maximum brightness if USB-PD negotiation fails (if running from 5 V)
  * `is_clk_inverted`: if `false`, data changes on the rising clock edge. If `true`, data is stable on the rising clock edge (most panels need `true`)
  * `clkm_div_num`: sets the I2S clock divider from 2 to 128. Set it too high and get flicker, too low get ghost pixels. Flicker can be improved at the cost of color depth by reducing `BITPLANE_CNT` in `rgb_led_panel.h`.
  `"clkm_div_num": 4` corresponds to a 10 MHz pixel clock
  * `max_frame_rate`: the global maximum frame-rate limit in [Hz]. The background shader is updated at this rate. If the value is too large, freertos will become unresponsive
  * `is_gamma`: apply gamma correction to LED brightness
  * `is_locked`: stop screen updates while the clock numerals are written. Prevents tearing artifacts at the cost of a short freeze of the background shader

### `delays` section
controls delays between random animations, color and font changes.
They are all specified in [seconds]. The defaults are:

  * Randomize clock font every 3600 s (60 min)
  * Randomize outline color every 600 s (10 min)
  * Play a new pinball animation every 15 s
  * Randomize the background shader every 300 s (5 min). Set `shader` to <= 0 to always have a black background

### `power` section
controls the display brightness. Set the `mode` parameter to 0, 1 or 2 to select the control mode.

__mode 0: constant brightness__

Brightness is always the same and specified in `day.p` as a number from 0 to 127.

__mode 1: use ambient light sensor__

Brightness is set according to the measured ambient light. The sensor returns a number `x` from 0 - 4095 for a measured light level of roughly 10 - 1000 lux.

A linear equation is used to calculate the brightness: `p = (x - offset) / divider`.

The calculated value is limited to the range of `min_limit` to `max_limit` and applied as the panel brightness.

__mode 2: time based switching__

Brightness is set according to the current time. There is a `day` and `night` value, specified in `p` as a number from 0 to 127.

  * In the example, day-mode starts at `9:00` with brightness `20`
  * night-mode starts at `22:45` with brightness `2`

### `wifis` section
Add one or more wifi credentials. The key should be set to the SSID, the value to the wifi password.
If disconnected, the ESP will try to connect to the known wifi with the strongest signal every minute.

### `hostname`
The hostname, which is the address where the ESP is reachable from the browser, can be customized here.

### `log_level`
Configure the esp-idf logging library. The key is the logging tag or `*` for everything.

The value the logging level

  * 0 = no logging
  * 1 = errors
  * 2 = warning
  * 3 = info
  * 4 = debug

See https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/log.html

### `timezone`
is the local timezone in [TZ format](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html). Look it up [here](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).

### `ntp_host`
The server used for time synchronization.

# TODO
  * fix hardware errors (4 botch wires), use MMC pins for SD card
  * http API support (easy use from curl)
  * wifi button support (access point on button press)
  * mqtt support?
  * update image from serial terminal
