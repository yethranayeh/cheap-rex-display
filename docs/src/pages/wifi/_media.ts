// The leading underscore keeps Astro's router from treating this as an endpoint.
import { mediaSrc } from "../../config/site";

export const improvShots = {
  pickPort: {
    src: mediaSrc("v1792946874/pick-serial_izb3fu.png"),
    alt: "Chrome asking which serial port improv-wifi.com may connect to, with USB Serial (cu.usbserial-3130) underlined among Bluetooth and debug ports.",
    width: 882,
    height: 908,
    title: "Pick the port",
    label: "Preview the serial port picker",
    caption:
      "The browser lists the serial ports. The board is the USB Serial one; on a Mac its name starts with cu.usbserial.",
  },
  configure: {
    src: mediaSrc("v1792946874/configure-wifi_vcgkdd.png"),
    alt: "The Configure Wi-Fi dialog showing the board's name, firmware, version and chip, with fields for the network and its password.",
    width: 916,
    height: 1048,
    title: "Configure Wi-Fi",
    label: "Preview the Wi-Fi form",
    caption:
      "The board answers with its name and version, and lists the networks it can see. Pick yours and enter its password.",
  },
  provisioned: {
    src: mediaSrc("v1792946874/provisioned_wgvhkk.png"),
    alt: "The dialog saying Provisioned!, with buttons for Close, Change Wi-Fi and Visit Device.",
    width: 878,
    height: 888,
    title: "Provisioned",
    label: "Preview the success screen",
    caption:
      "The board joined and saved the network. It restarts onto it; Visit Device opens the board's page.",
  },
};
