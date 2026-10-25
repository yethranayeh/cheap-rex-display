# cheap-rex-display

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/branding/banner-dark.svg">
    <img title="The skillful retracing of the Rex icon, carefully handcrafted on paper" src="assets/branding/banner-light.svg" alt="Cheap Rex Display, traced and drawn by hand in pen on Superlogical's Rex" width="560">
  </picture>
</p>

A cheap [Rex](https://www.superlogical.com/rex/docs/installation) display for coding agents. It shows the agents running in Rex sessions on a Cheap Yellow Display (CYD) device.

> Not affiliated with or endorsed by Superlogical. The Rex drawing is meant as a tribute.

When an agent stops to wait for you, the LED pulses red so you know your agent is in big trouble or just has a few blockers that you need to clear:

<p align="center"><img src="https://res.cloudinary.com/drwbqceye/image/upload/v1792852978/opencode-status-cycle_r6f1ad.webp" alt="opencode going from idle to working to permission and back, with the LED following" width="320"></p>

Experimental work in progress.

## Tested hardware

This is what the project is being built and tested on. Anything else is **untested**.

- Board: ESP32-2432S028
- Chip: ESP32-D0WD-V3, revision 3.1, 4 MB flash, CH340 USB-serial
- Arduino board: "ESP32 Dev Module" (`esp32:esp32:esp32`).
- Toolchain: arduino-cli 1.3.1, ESP32 core 3.3.7, TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4
- Upload speed: 460800. My agent (Claudia) says the default 921600 fails on this board (`UploadSpeed=460800`)
- Rex on macOS 15.7 (Apple silicon): build `0.0.1792697926-gbd4573` (22.10.2026). The watchers run here, for this server and, with `rex -S`, for the Linux server
- Rex on Ubuntu 24.04 (x86_64): build `0.0.1792697160-g951cba`, watched from the Mac

Other CYD variants may need different display settings and setups. If your screen shows wrong colors or stays blank, start with `config/TFT_eSPI/User_Setup.h`.

Known values for this board, if you write your own code:

- Display: ILI9341 (`ILI9341_2_DRIVER`), rotation 1, backlight on GPIO 21. No inversion and no RGB order change
- Touch (XPT2046) has its own SPI pins: IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33. Raw X 275-3630 maps to 0-319 and raw Y 380-3730 to 0-239
- RGB LED: GPIO 4 red, 16 green, 17 blue. Active low: LOW turns a color on

## Install from the browser

On [cheap-rex-display.pages.dev/install](https://cheap-rex-display.pages.dev/install/), Chrome or Edge flashes the latest release onto the board over USB, then asks for your Wi-Fi. Building it yourself still works as before, see below.

## Setup from zero

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/) (`brew install arduino-cli` on macOS)
2. Install the ESP32 core (downloads about 1.5 GB and takes about 5 GB installed), **TFT_eSPI** and **XPT2046_Touchscreen**, at the versions above. Wi-Fi setup needs nothing more: `Preferences`, `DNSServer` and `WebServer` come with the core, and the firmware speaks Improv Wi-Fi itself, without a library:

   ```sh
   URL=https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core update-index --additional-urls $URL
   arduino-cli core install esp32:esp32@3.3.7 --additional-urls $URL
   arduino-cli lib install "TFT_eSPI@2.5.43"
   arduino-cli lib install "XPT2046_Touchscreen@1.4"
   ```

3. Plug in the board and find its port with `arduino-cli board list`.
4. Build and upload the firmware, see [Firmware](#firmware).
5. Install [Rex](https://www.superlogical.com/rex/docs/installation) and start the watcher, see [Watcher](#watcher).

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

`firmware/cheap-rex-display` joins your Wi-Fi as `crd.local` and lists the agent status it receives over HTTP, with most urgent appearing first. The agent that has waited the longest is listed first among other agents that are also waiting.

Rows are sized for a finger with the option for a compact look. The button at the right of the header switches to compact rows, wihch can be used to pack more information and can still be used comfortably if you have a stylus. The pagination buttons under the list also follow the view choice.

The names at the right of the header are the Rex servers whose watchers send to the board. Each is white while its watcher is in touch, and turns black when it goes quiet (see [Watcher](#watcher)).

Each row shows at its right edge how long the agent has been in its current state, such as `4m` or `1h 12m`. Rows in a state for less than a minute show no time. The time counts from the last change of state, so heartbeats and new messages in the same state don't reset it.

The LED speaks for each agent that waits for you, one after another, then pauses:

- **permission**: a quick red double blink
- **question**: a long amber pulse
- **auth**: a long orange pulse

If nobody is waiting, it glows green for a moment when an agent finishes, breathes blue while agents work, and stays magenta on an error. `LedStyle` in the firmware switches each of these: one signal for all instead of one per agent, a single red pulse instead of the double blink (or the double only after a few minutes), steady blue, steady green.

On boot, there is an animation while the board joins Wi-Fi. You can tap to skip it or admire my handwriting. The header comes in three styles: `HEADER_REX` at the top of the firmware picks `REX_PIXEL` (default), `REX_SOFT` or `REX_BOLD`.

<p align="center"><img src="https://res.cloudinary.com/drwbqceye/image/upload/v1792852972/opencode-permission-led_ymwmer.webp" alt="opencode asks for permission on the Mac, and the board shows it on top with the LED turning red" width="480"></p>

1. Optional: create a `secrets.h` file like shown in `secrets.h.example` and fill in your Wi-Fi credentials. Without it, set up Wi-Fi on the board after uploading, see [Wi-Fi](#wi-fi)
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

### Wi-Fi

The board keeps its Wi-Fi in flash. On boot it joins, in this order:

1. the network saved on the board, through the setup hotspot or Improv
2. the one in `secrets.h`, if you built with it
3. none: it opens its setup hotspot

**Setup hotspot.** With nothing to join, the board says so at the bottom of the boot animation, then opens its own Wi-Fi. The screen shows its name (`crd-setup-` and four letters of the board's address) and a password, new each time the hotspot opens:

1. On a phone or computer, join that Wi-Fi with the password from the screen.
2. A setup page opens by itself. If it doesn't, go to `http://192.168.4.1`.
3. Pick your network, enter its password and choose **Save**. The board only sees 2.4 GHz networks. **Scan again** looks once more.

The board joins the network, saves it and restarts onto it. The hotspot goes away. If it can't join, the board's screen says so and the hotspot stays: join it again and retry.

The hotspot also opens when the board can't join its network for 3 minutes. This can happen if you move it somewhere else. While no device is on the hotspot, the board keeps trying its old network, and restarts onto it once it's back.

**Improv over USB.** The firmware speaks [Improv Wi-Fi](https://www.improv-wifi.com/serial/) over its serial port at all times. An installer built on [ESP Web Tools](https://esphome.github.io/esp-web-tools/) offers to set up Wi-Fi right after flashing. On a board that is already flashed, the **Improv via Serial** button on [improv-wifi.com](https://www.improv-wifi.com/) does the same in Chrome or Edge. Close any serial monitor first. Once the board joins, it saves the network and restarts.

**`secrets.h`** is optional and works as before. A network saved on the board comes first, so after forgetting it the board goes back to the one in `secrets.h`.

**Forgetting Wi-Fi.** Hold a finger on the boot animation. After a second the bottom line counts down, and after 5 seconds the board asks whether to forget the saved network. Tap **Forget** to forget it and restart, or **Keep**, or wait 10 seconds, to keep it. A short tap still skips the animation. Forgetting is only offered on the board itself, never over HTTP, and only when a network is saved on the board.

A normal upload keeps the saved network. A full flash erase (`esptool.py erase_flash`, or an installer's "erase device" option) wipes it along with the board's other settings. Updating from the website keeps it: the installer writes the bootloader, partition table, boot selector and app, and leaves NVS alone. A new install from the website, over other firmware or a board the installer can't identify, erases the whole flash first, and the installer asks for Wi-Fi again right after.

The board keeps its settings in the `Preferences` (NVS) namespace `crd`: `wifi_ssid` and `wifi_pass`. The firmware's header comment lists the keys. Passwords never go to the serial port or the screen.

## Watcher

`watcher/cheap-rex-display.lua` is a Rex Lua script. It listens for program status changes in every session of a Rex server and sends each one to the board. Run it in a Rex pane and leave it open:

```sh
rex do watcher/cheap-rex-display.lua --all
```

`--all` delivers events from every session, not only the one the script runs in. Two optional arguments can be provided:

- `source=<name>`: the name this Rex server gets on the display, `mac` by default.
- `url=<address>`: the board's address, which is `http://crd.local` by default.

On start, the script sends the current status of every pane. After that it sends each change, and a heartbeat every 15 seconds. Closing a pane, or the tab it is in, removes its row, and renaming a session renames it on the display too.

The board keeps everything in memory, so a restart empties it. When that happens, or the board drops off Wi-Fi for a while, the next heartbeat notices and the script sends everything again, within about 15 seconds.

If a Rex server goes quiet for 60 seconds, for example a laptop that went to sleep, its rows turn grey and its name in the header turns black. Grey rows don't light the LED.

Tap a row to bring its pane to the front: the Rex app switches to that session and tab. The row flashes a white outline when its watcher took the tap, and a grey one when no watcher is listening. The script keeps a request open to the board's port 81 for this (`GET /taps?source=<name>`), and opens it again on the next heartbeat if it drops.

<p align="center"><img src="https://res.cloudinary.com/drwbqceye/image/upload/v1792852972/tap-to-focus_qsetsf.webp" alt="Tapping rows on the board, and the Rex app on the monitor behind it switching to each one's pane, including one on another server" width="640"></p>

### Several Rex servers

Run one watcher per Rex server, each with its own `source`. With `rex -S <label>` (labels from `rex hosts`), the script runs on your machine and only talks to the other server through Rex, so the board only has to be reachable from your machine:

```sh
rex do watcher/cheap-rex-display.lua --all source=mac
rex -S <label> do watcher/cheap-rex-display.lua --all source=work
```

The header lists every source the board has heard from, up to four.

## Troubleshooting

- **Upload fails with "The chip stopped responding"**: upload at 460800 (`:UploadSpeed=460800` in the `--fqbn`). If the port is busy, close any serial monitor first.
- **Blank screen or wrong colors**: your board is likely a different CYD variant. Start with `config/TFT_eSPI/User_Setup.h`.
- **Header stays at "Wi-Fi connecting..."**: the ESP32 only joins 2.4 GHz networks. Open a serial monitor at 115200 baud: the board prints which network it joins (saved or `secrets.h`), then `Wi-Fi up: <address>` or `Wi-Fi down`. After 3 minutes it opens the setup hotspot, or hold the boot animation to forget the saved network.
- **Wrong network after setting one up**: a network saved on the board comes before `secrets.h`. Forget it to go back to `secrets.h`.
- **The setup page doesn't open**: go to `http://192.168.4.1` while your device is on the hotspot. Some phones and computers leave a Wi-Fi without internet after a while; tell yours to stay on it.
- **The setup screen says "Couldn't join"**: check the password, and that the network is 2.4 GHz. Join the hotspot again and retry.
- **No networks on the setup page**: tap **Scan again**. Hidden networks aren't listed.
- **The installer doesn't offer Wi-Fi setup, or improv-wifi.com says Improv isn't detected**: close any serial monitor, since only one program can hold the port. Press the board's reset button and try again.
- **Wi-Fi is gone after an update**: the flash was fully erased. Set it up again.
- **`crd.local` takes 5 seconds**: macOS also asks for an IPv6 address the board never sends. Use `curl -4`. The watcher watcher already does this as documented above.
- **`crd.local` not found**: `.local` names (mDNS) only work on the same network. Try using the address in the board's header instead: `url=http://<address>`.
- **The watcher logs `/status failed`**: the board can't be reached. The watcher sends everything again once it is back.
- **Rows stay grey**: that server's watcher stopped. Start it again.
- **A stopped source stays in the header**: the board forgets sources only when it restarts.
- **Your own Rex script fails with "stream is not attached to session"**: `--all` delivers events from every session, but viewing another session needs `rex.call("session.attach", { session_id = ... })` first. The watcher does this.

## Development

### Screenshots

The board reads its screen back from the display and serves it as a 320×240 BMP:

```sh
curl -4 -o screen.bmp http://crd.local/screen.bmp
sips -s format png screen.bmp --out screen.png
```

You can use it to let your agent take a look at the board, because sometimes looking at it yourself is not enough. Team work makes the dream work!

### Releases

A `v*` tag builds the firmware in `.github/workflows/release.yml` and attaches the install page's images and manifest, a merged image for esptool, and `firmware.json` with each file's size and sha256. Bump `FIRMWARE_VERSION` in the sketch, commit, then tag and push:

```sh
python3 scripts/release-assets.py v0.2.0   # exits if the tag isn't v + FIRMWARE_VERSION
git tag v0.2.0 && git push origin v0.2.0
```

The tag has to match, because the board reports the version to the installer and ESP Web Tools compares it with the manifest's to offer an update. A `-` in the tag (`v0.2.0-rc.1`) makes a prerelease. Then fetch it into the website, see `docs/README.md`.

## Plan

- A Rex Lua script that watches a Rex server for program status changes and sends them to the CYD.

The display only shows status. Nothing on it can answer or approve anything in an agent session. Yet.

## License

MIT, see [LICENSE](LICENSE).
