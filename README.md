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

## Firmware

`firmware/cheap-rex-display` joins your Wi-Fi as `crd.local` and lists the agent status it receives over HTTP, with most urgent appearing first. The LED pulses red while an agent is blocked.

1. Create a `secrets.h` file like shown in `secrets.h.example` and fill in your Wi-Fi credentials
2. Compile and upload with the display setup from above. Find your port with `arduino-cli board list`

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:UploadSpeed=460800 \
  --build-property "compiler.cpp.extra_flags=-DUSER_SETUP_LOADED -include $PWD/config/TFT_eSPI/User_Setup.h" \
  --upload -p <port> firmware/cheap-rex-display
```

Try it without Rex. The header shows the board's address once it is on Wi-Fi:

```sh
curl -4 -d source=test -d key=1 -d session=demo -d app=claude -d state=blocked -d kind=permission http://crd.local/status
curl -4 http://crd.local/
curl -4 -d source=test -d key=1 -d state=clear http://crd.local/status
```

`-4` is for macOS to expect IPv4 answer from the board without waiting for an IPv6 answer that CYD never even sends.

## Watcher

`watcher/cheap-rex-display.lua` is a Rex Lua script. It listens for program status changes in every session of a Rex server and sends each one to the board. Run it in a Rex terminal and leave it open:

```sh
rex do watcher/cheap-rex-display.lua --all
```

`--all` delivers events from every session, not only the one the script runs in. Two optional arguments can be provided:

- `source=<name>`: the name this Rex server gets on the display, `mac` by default.
- `url=<address>`: the board's address, whcih is `http://crd.local` by default.

On start, the script sends the current status of every tab. After that it sends each change, and a heartbeat every 15 seconds. Closing a tab removes its row.

The board keeps everything in memory, so a restart empties it. When that happens, or the board drops off Wi-Fi for a while, the next heartbeat notices and the script sends everything again, within about 15 seconds.

If a Rex server goes quiet for 60 seconds, for example a laptop that went to sleep, its rows turn grey and its name in the header turns black. Grey rows don't light the LED.

## Plan

- A Rex Lua script that watches a Rex server for program status changes and sends them to the CYD.

The display only shows status. Nothing on it can answer or approve anything in an agent session. Yet.

## License

MIT, see [LICENSE](LICENSE).
