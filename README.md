# The ESP32 Pinball RGB Animation (espirgbani) clock

The [old firmware](https://github.com/yetifrisstlama/Espirgbani) was based on obsolete libraries and became difficult to maintain. This is a re-write based on platform.io and the Arduino framework.

![clock](https://github.com/yetifrisstlama/Espirgbani/raw/master/pcb/pdf/front.jpg)

[Some older writeup here](http://yetifrisstlama.blogspot.com/2018/02/the-esp32-pinball-rgb-matrix-animation.html).

# Building
Needs [platform.io](https://platformio.org/) installed. 

WIFI credentials can optionally be set in `platform.ini`. These will be overwritten by the `wifi_ssid` and `wifi_pw` keys when they exist in `settings.json`.

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
Once connected to the network, firmware can be uploaded [over-the-air](https://docs.platformio.org/en/latest/platforms/espressif32.html#over-the-air-ota-update):

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
Pre-built font and animation files can be found [here](https://github.com/yetifrisstlama/Espirgbani/releases/tag/v1.0). Copy these to the FAT32 formated SD card.

## `settings.json`
If this file does not exist or cannot be parsed, a new file with default settings will be created.

Once the clock is connected to the network, `settings.json` can be edited in the web-interface at `http://espirgbani.local/`.

```json
{
    "_wifi_ssid": "",
    "_wifi_pw": "",
    "hostname": "espirgbani",
    "enable_ota": false,
    "timezone": "PST8PDT,M3.2.0,M11.1.0",
    "panel": {
        "test_pattern": true,
        "brightness":  10,
        "is_clk_inverted": true,
        "column_swap": false,
        "latch_offset": 0,
        "extra_blank": 1,
        "clkm_div_num": 3
    },
    "delays": {
        "font":  60,
        "color": 10,
        "ani":   15
    }
}
```
### `panel` section
Not all LED panels are the same. Here the timing parameters of the I2S panel driver can be configured.

  * `test_pattern`: if true, enters a LED panel test mode instead of normal operation
  * `tp_brightness`: brightness of the test pattern, from 1 to 127. Current draw gets ridiculous for the higher values
  * `is_clk_inverted`: if `false`, data changes on the rising clock edge. If `true`, data is stable on the rising clock edge (most panels need `true`)
  * `column_swap`: if true, swap each pair of vertical columns
  * when `latch_offset = 0`: latch row-data with last pixel. For positive / negative numbers the latching happens n clock cycles earlier / later. Shifts the image horizontally
  * `extra_blank` adds N additional delay cycles after latching before enabling the LEDs to prevent ghosting artifacts from one row to another
  * `clkm_div_num` sets the I2S clock divider from 1 to 128. Set it too high and get flicker, too low get ghost pixels.
  `"clkm_div_num": 3` corresponds to a 10 MHz pixel clock

### `delays` section
controls delays between random animations, color and font changes.

  * Changes the clock font every 60 minutes
  * Changes color of the clock font every 10 minutes
  * Plays a new pinball animation every 15 seconds

### `timezone`
is the local timezone in [TZ format](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html). Look it up [here](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).
