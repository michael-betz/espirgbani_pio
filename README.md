# The ESP32 Pinball RGB Animation clock

The [old firmware](https://github.com/yetifrisstlama/Espirgbani) was based on obsolete libraries and became difficult to maintain. This is a re-write based on platform.io and the Arduino framework.

![clock](https://github.com/yetifrisstlama/Espirgbani/raw/master/pcb/pdf/front.jpg)

[Video of how it looks like](https://www.youtube.com/watch?v=0dwTC5q5t4M)

[Older writeup](http://yetifrisstlama.blogspot.com/2018/02/the-esp32-pinball-rgb-matrix-animation.html)

# Building
Needs [platform.io](https://platformio.org/) installed. 

WIFI credentials can optionally be set in `platform.ini`. At run-time these will be overwritten by the `wifi_ssid` and `wifi_pw` entries in `settings.json`, if they exist.

To install the toolchain and build the project:

```bash
$ pio run
```

Connect a FTDI 3.3V USB to serial cable, hold the flash button and upload the SPIFFS file system:

```bash
$ pio run -t uploadfs
```

Upload the firmware and show the serial console:

```bash
$ pio run -t upload -t monitor
```

Once connected to the network, firmware can be uploaded [over-the-air](https://docs.platformio.org/en/latest/platforms/espressif32.html#over-the-air-ota-update). This requires `"enable_ota": true`  in settings.json.

```bash
$ pio run -t upload --upload-port espirgbani.local
```

# SD card instructions
Format as FAT32, then copy the following files:
  * `./settings.json`
  * `./runDmd.img`
  * `./fnt/{d}.fnt`
  * `./fnt/{d}_0.bmp`

## Assets
Pre-built font and animation files can be found [here](https://github.com/yetifrisstlama/espirgbani_pio/releases). Copy these to the FAT32 formated SD card.

The bitmap fonts have been auto-generated from existing true-type fonts by a [bash script](https://github.com/yetifrisstlama/Espirgbani/blob/master/dev/generateFonts.sh) and [bmfont](http://www.angelcode.com/products/bmfont/).

## `settings.json`
If this file does not exist or cannot be parsed, a new file with default settings will be created.

Once the clock is connected to the network, `settings.json` can be edited in the web-interface at `http://espirgbani.local/`.

```json
{
    "panel": {
        "test_pattern": true,
        "tp_brightness": 10,
        "is_clk_inverted": true,
        "column_swap": false,
        "latch_offset": 0,
        "extra_blank": 1,
        "clkm_div_num": 3,
        "lock_frame_buffer": false,
        "max_frame_rate": 30
    },
    "delays": {
        "font": 3600,
        "color": 600,
        "ani": 15,
        "shader": 300
    },
    "power": {
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
    "_wifi_ssid": "",
    "_wifi_pw": "",
    "hostname": "espirgbani",
    "enable_ota": true,
    "timezone": "PST8PDT,M3.2.0,M11.1.0"
}
```
### `panel` section
Not all LED panels are the same. Here the timing parameters of the I2S panel driver can be configured.

  * `test_pattern`: if `true`, enters a LED panel test mode instead of normal operation
  * `tp_brightness`: brightness of the test pattern, from 1 to 127. Current draw gets ridiculous for the higher values
  * `is_clk_inverted`: if `false`, data changes on the rising clock edge. If `true`, data is stable on the rising clock edge (most panels need `true`)
  * `column_swap`: if true, swap each pair of vertical columns
  * `latch_offset`: when 0, latch row-data with last pixel. For positive / negative numbers the latching happens N clock cycles earlier / later. Shifts the image horizontally
  * `extra_blank`: adds N additional delay cycles after latching before enabling the LEDs to prevent ghosting artifacts from one row to another
  * `clkm_div_num`: sets the I2S clock divider from 1 to 128. Set it too high and get flicker, too low get ghost pixels. Flicker can be improved at the cost of color depth by reducing `BITPLANE_CNT` in `rgb_led_panel.h`.
  `"clkm_div_num": 3` corresponds to a 10 MHz pixel clock
  * `max_frame_rate`: the global maximum frame-rate limit in [Hz]. The background shader is updated at this rate. If the value is too large, freertos will become unresponsive
  * `lock_frame_buffer`: when true, prevent tearing artifacts at the cost of choppier animation playback

### `delays` section
controls delays between random animations, color and font changes. 
They are all specified in [seconds]. The defaults are:

  * Randomize clock font every 3600 s (60 min)
  * Randomize outline color every 600 s (10 min)
  * Play a new pinball animation every 15 s
  * Randomize the background shader every 300 s (5 min). Set `shader` to <= 0 to always have a black background

### `power` section
controls the display brightness according to current time. Brightness is specified in `p` as a number from 0 to 127.

  * day-mode starts at `9:00` with brightness `20`
  * night-mode starts at `22:45` with brightness `2`

### `timezone`
is the local timezone in [TZ format](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html). Look it up [here](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).

# FASTSEEK hack
The frames of the pinball animations are sometimes played back in a non sequential order. Hence `fseek()` is required to jump to a specific frame. Unfortunately, for files > 100 MB on a FAT32 SD card, this takes __hundreds of ms__ with the default settings.

See also:
https://github.com/espressif/arduino-esp32/issues/4097

Seeking performance can be substantially improved by enabling the `FF_USE_FASTSEEK` option in `ffconf.h`. However when using Arduino, it uses pre-compiled esp-idf libraries and there is no easy way to change this macro. The .a files are actually checked into git and need to be trusted blindly. What an ideal place to put a wifi back-door btw.

Anyhow, I rely on the linker to override the ff.c and ffconf.h files with the patched versions from `fseek_hack/`, which have FASTSEEK always enabled.

This works because the linker looks into the files in `src/` first and only if it cannot find a symbol there, it will search the libraries.

This should work as long as the version of fatfs does not change upstream in esp-idf.

If the hack causes problems, it can be disabled in `platformio.ini`.

