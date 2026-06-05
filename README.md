# shot-timer

Switch-controlled coffee shot timer for the Seeed XIAO nRF52840 with SSD1306 OLED display and deep sleep.

## > [!IMPORTANT]

* Rotate case by 115.20 degrees in the slicer to have a nice front plate
* Rotate the backpanel by 79.8 degrees to make it sit flat on the bed

## Wiring

```
 Seeed XIAO nRF52840
 ┌─────────────────────┐
 │                 3V3 ├─────────────────── VCC ──┐
 │                 GND ├──────────┬──────── GND   ├── SSD1306
 │         D4 / (SDA)  ├──────────│──────── SDA   │    OLED
 │         D5 / (SCL)  ├──────────│──────── SCL ──┘
 │                  D1 ├───[ A ]  │
 │                BAT+ ├─────│────│──────────────── Battery +
 │                BAT- ├─────│────│──────────────── Battery -
 └─────────────────────┘     │    │
                             │    │
                          ┌──┴──┐ │
                          │ SW  │ │
                          └──┬──┘ │
                           [ B ]  │
                             └────┘ (GND)

 No external resistor needed — internal pull-up on D1 is enabled in firmware.
 Battery charges automatically when USB is connected.
```

| XIAO pin | Connects to |
|---|---|
| `3V3` | OLED VCC |
| `GND` | OLED GND, Switch Leg B |
| `D4 (SDA)` | OLED SDA |
| `D5 (SCL)` | OLED SCL |
| `D1` | Switch Leg A |
| `BAT+` | Battery positive |
| `BAT-` | Battery negative |

Most SSD1306 breakout boards label their pins `VCC GND SCL SDA` left-to-right — double check yours before wiring.

> [!WARNING]
> Use `3V3` for OLED power, **not** `VUSB`. VUSB is only live when USB is connected — the OLED will be dead on battery if wired to VUSB.

## Behaviour

| Action | Result |
|---|---|
| Short press (stopped) | Start timer |
| Short press (running) | Pause timer |
| Hold 2 s | Reset timer to 0 |
| Timer reaches 99 s | Deep sleep |
| Paused / stopped for 60 s | Deep sleep |

Press the switch to wake from deep sleep.

## Case

* The case has been adapted from <https://makerworld.com/en/models/1393341-desktop-wifi-informer-esp8266-oled-ssd1306?from=search#profileId-1443943>
* Seeed XIAO BLE compatability has been added
* My SSD1306 has been made compatible to the front panel
* A hole has been added on top to take an MX switch
