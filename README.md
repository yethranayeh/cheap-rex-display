# cheap-rex-display

A cheap [Rex](https://www.superlogical.com/rex/docs/installation) display for coding agents. It shows the agents running in Rex sessions on a Cheap Yellow Display (CYD) device. When an agent stops to wait for you, the LED pulses red so you know you are agent is in big trouble or just has a few blockers that you need to clear.

Experimental work in progress.

## Plan

- Firmware for the CYD that joins your Wi-Fi and shows the status it is sent over HTTP.
- A Rex Lua script that watches a Rex server for program status changes and sends them to the CYD.

The display only shows status. Nothing on it can answer or approve anything in an agent session. Yet.

## License

MIT, see [LICENSE](LICENSE).
