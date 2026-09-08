# ESP32-S3 Firmware

Three PlatformIO environments share one source folder. Pick one with `-e`.

| Env | Source | What it does |
|-----|--------|--------------|
| `cloud` (default) | `src/cloud.cpp` | Firebase-connected controller: IR climate control, Wake-on-LAN, power-outage ping, DHT11 reporting |
| `control` | `src/main.cpp` | LAN-only web UI for IR climate control — useful for bench testing without Firebase |
| `decode` | `src/decode.cpp` | IR receiver dumper, used once to identify the air-conditioner's protocol |

## First-time setup

`config.h` is gitignored because it holds Wi-Fi and Firebase account
credentials. Copy the example and fill it in:

```bash
cp src/config.example.h src/config.h
```

## Wiring

| Component | Pin (component) | ESP32-S3 GPIO |
|-----------|-----------------|---------------|
| KY-022 (IR receiver) | S / OUT | GPIO 15 |
| KY-005 (IR transmitter) | S / DAT | GPIO 4 |
| DHT11 | DATA / S | GPIO 5 |

All `+` / `VCC` → **3V3**, all `-` / `GND` → **GND**.

> The KY-005 transmitter is a single unamplified LED, so its range is short.
> Keep it within roughly 1–2 m of the air conditioner and in line of sight.

## Identifying your air conditioner's IR protocol

Different units speak different IR protocols, so the protocol has to be
identified once per unit:

```bash
pio run -e decode -t upload && pio device monitor
```

Point the original remote at the KY-022 and press a button. The serial output
prints the detected protocol and the decoded climate state:

```
Protocol  : ELECTRA_AC
AC State  : Power: On, Mode: 1 (Cool), Temp: 24C, Fan: 0 (Auto) ...
```

If it prints `Protocol : UNKNOWN`, the protocol is not in the library's table
and you would need to fall back to replaying the raw timing array that
`decode.cpp` also dumps.

Set `AC_PROTOCOL` in `src/cloud.cpp` (and `src/main.cpp`) to the name you got.
This repository is configured for `ELECTRA_AC`, which is what the unit it was
built against uses.

## Flashing

```bash
pio run -e cloud -t upload && pio device monitor
```

For the LAN-only web UI instead:

```bash
pio run -e control -t upload && pio device monitor
```

The serial monitor prints the device IP; the web UI is served at `http://<ip>/`
or `http://homenode.local/`.
