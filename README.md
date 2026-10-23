# cheap-rex-display

<p align="center"><img title="The skillful retracing of the Rex icon, carefully handcrafted on paper." src="assets/cheap-rex-display-logo.jpg" alt="Superlogical's Rex, drawn by hand in pen on paper" width="320"></p>

A cheap [Rex](https://www.superlogical.com/rex/docs/installation) display for coding agents. It shows the agents running in Rex sessions on a Cheap Yellow Display (CYD) device. When an agent stops to wait for you, the LED pulses red so you know your agent is in big trouble or just has a few blockers that you need to clear.

Experimental work in progress.

## Tested hardware

This is what the project is being built and tested on. Anything else is **untested**.

- Board: ESP32-2432S028
- Chip: ESP32-D0WD-V3, revision 3.1, 4 MB flash, CH340 USB-serial
- Arduino board: "ESP32 Dev Module" (`esp32:esp32:esp32`).
- Toolchain: arduino-cli 1.3.1, ESP32 core 3.3.7, TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4
- Upload speed: 460800. My agent (Claudia) says the default 921600 fails on this board (`UploadSpeed=460800`)

Other CYD variants may need different display settings and setups. If your screen shows wrong colors or stays blank, start with `config/TFT_eSPI/User_Setup.h`.

Known values for this board, if you write your own code:

- Display: ILI9341 (`ILI9341_2_DRIVER`), rotation 1, backlight on GPIO 21. No inversion and no RGB order change
- Touch (XPT2046) has its own SPI pins: IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33. Raw X 275-3630 maps to 0-319 and raw Y 380-3730 to 0-239
- RGB LED: GPIO 4 red, 16 green, 17 blue. Active low: LOW turns a color on

## Display setup

`config/TFT_eSPI/User_Setup.h` holds the **TFT_eSPI** settings for the board above. **TFT_eSPI** normally reads its settings from its own library folder, which every sketch shares and a library update overwrites. To leave the library alone, pass the file in as a build flag. Define `USER_SETUP_LOADED` so the library skips its own setup, and include our file instead:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-DUSER_SETUP_LOADED -include /full/path/to/config/TFT_eSPI/User_Setup.h" \
  path/to/sketch
```

Claudia says to use an absolute path, and add `:UploadSpeed=460800` to the `--fqbn` when you upload. This replaces any other `compiler.cpp.extra_flags` you set. A path with spaces needs extra quoting.

Without **arduino-cli**, copy the file over `User_Setup.h` in the **TFT_eSPI** library folder instead, and again after each library update.

## Plan

- Firmware for the CYD that joins your Wi-Fi and shows the status it is sent over HTTP.
- A Rex Lua script that watches a Rex server for program status changes and sends them to the CYD.

The display only shows status. Nothing on it can answer or approve anything in an agent session. Yet.

## License

MIT, see [LICENSE](LICENSE).
