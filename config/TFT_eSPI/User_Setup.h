// TFT_eSPI setup for the ESP32-2432S028 Cheap Yellow Display.
// See the README for how to use it.

#define USER_SETUP_INFO "cheap-rex-display"

// Display controller. The "_2" driver is the variant that works on this board.
#define ILI9341_2_DRIVER

// Panel size in portrait orientation.
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Display SPI pins. The display sits on the ESP32's second SPI port (HSPI).
#define USE_HSPI_PORT
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1   // reset line is tied to the board reset, not a GPIO

// Backlight, switched on with a high level.
#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH

// No TFT_INVERSION_ON and no TFT_RGB_ORDER: colors are right without them.

// SPI speeds in Hz.
#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000

// Fonts compiled in. Drop the ones you do not use to save flash.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
