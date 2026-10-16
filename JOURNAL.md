## Day 1 – [date]: Planning InkDeck
**Time spent:** ~ 1 hours

- Decided to build a DIY e-reader with physical page buttons
- Picked the LilyGo T5 4.7" E-Paper S3 as the base (ESP32-S3, 960×540 e-paper, SD slot)
- Designed the block diagram: custom grip PCB with Prev/Next/Menu buttons,
  haptic motor and power switch
- Researched the pinout: free GPIOs are IO10, IO39, IO45, IO48
- Learned that only RTC pins can wake the ESP32-S3 from deep sleep → Menu on IO10
- Learned IO45 is a strapping pin → used it for the motor with a pull-down
- Made a parts list (~$130–170 AUD)

<img width="1000" height="1000" alt="image" src="https://github.com/user-attachments/assets/ce75c77c-bef0-4938-bd06-990af40a0df5" />


### Next steps
- [ ] Find the physical pin numbers on the 40-pin header
- [ ] Draw the schematic in KiCad

## Day 2 – [date]: InkDeck schematic + PCB
**Time spent:** ~ 6 hours

### What I did
- Drew the grip PCB schematic in KiCad:
  - Battery in → slide power switch → battery out to the LilyGo
  - 3 page buttons (Prev / Next / Menu), with a 10k pull-up on Menu so it can wake
    the ESP32 from deep sleep
  - Haptic motor driven by an AO3400 MOSFET, with a 1N4148 flyback diode and
    a 10k gate pull-down (keeps IO45 low at boot, since it's a strapping pin)
  - 6-pin JST-SH connector to the LilyGo GPIOs (IO10, IO39, IO45, IO48, 3V3, GND)
- Laid out a 25 × 67 mm board to sit beside the LilyGo inside the case
- Power on the front layer, button signals on the back, GND fill on both sides
- Fixed DRC errors → **0 errors, 0 unconnected**
- Exported Gerbers and got a JLCPCB quote

### Problems I solved
- MOSFET pins wired wrong at first (gate was on GND). Learned GSD = pin 1/2/3
- Accidentally put text on the copper layer, which caused 15+ clearance errors
- SMD parts (J3, R1) only have front pads, so back-layer traces needed vias
- Back-layer traces stopped just short of the button pads → extended them

### J3 pin map (for wiring the cable later)
| J3 | Signal | LilyGo |
|---|---|---|
| 1 | +3.3V | 3V3 |
| 2 | GND | GND |
| 3 | BTN_MENU | IO10 |
| 4 | BTN_PREV | IO39 |
| 5 | MOTOR | IO45 |
| 6 | BTN_NEXT | IO48 |

<img width="824" height="567" alt="image" src="https://github.com/user-attachments/assets/51734021-7b59-4a79-9b58-29b04453e634" />
<img width="415" height="724" alt="image" src="https://github.com/user-attachments/assets/5c3be672-21ed-40ab-b6bf-e7dea0ae26f2" />


### Next steps
- [ ] Enclosure CAD
- [ ] Firmware (EPUB reader + buttons + deep sleep)
