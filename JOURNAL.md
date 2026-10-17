<img width="1125" height="707" alt="image" src="https://github.com/user-attachments/assets/1fdd8b21-7018-4f3a-9c10-1bf348d4c7ef" />## Day 1 – 14/10/2026: Planning InkDeck
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

## Day 2 – 15/10/2026: InkDeck schematic + PCB
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

## Day 3 – 17/10/2026: InkDeck enclosure CAD
**Time spent:** ~7 hours

### What I did
- Designed a handheld case for the LilyGo T5 4.7" + my grip PCB
- Started from a concept + example reference model and dimensioned drawing
  (made with AI help), then [modelled / adjusted] the parts myself in Fusion 360
- Set up **user parameters** for the board, battery and button positions so I
  can update everything once the real parts arrive
- Final size: **153 × 72 × 23.5 mm** (a bit smaller than a paperback)

### Parts
1. **Front shell**
   - 106 × 61 mm window for the e-paper screen
   - 3 button holes (Ø6.4) lined up with PREV / NEXT / MENU on the grip PCB.
     Positions taken straight from my KiCad layout (13.2 mm from the top, ~10 mm apart)
   - 4 standoffs with M2 heat-set inserts to hold the grip PCB
   - Cut-outs: power-switch slot (top), USB-C (left), microSD + LilyGo button
     pin-holes (bottom)
   - Small recess inside so the tall JST-PH battery connectors fit
2. **Back cover**
   - Fence that holds the LiPo battery behind the LilyGo
   - Raised grip ridge behind the buttons so it's comfy to hold one-handed
   - 4 × M2 screws into inserts in the corner bosses
3. **Button caps (×3)**
   - Plunger pokes out 1.2 mm; a 9 mm flange stops it falling out

### Decisions
| Decision | Why |
|---|---|
| Grip on the right, USB-C on the left | Thumb reaches the buttons; cable doesn't get in the way |
| Separate printed button caps | Tact switches sit 5 mm below the surface, so caps reach them |
| Battery behind the screen | Keeps the case narrow; battery + foam holds the LilyGo in place |
| M2 heat-set inserts | Small enough for a thin case, and it can be opened many times |
| Parametric model | Screen, USB-C and battery positions aren't confirmed yet |

### Problems / what I learned
- Had to work out the stack-up depth: screen glass → button height → PCB → battery
- JST-PH connectors were taller than the gap behind the front plate, so I added a recess
- [anything else you hit in Fusion, e.g. sketch constraints, fillets failing, etc.]

### Still to check (placeholders)
- Exact position of the e-paper active area on the LilyGo
- USB-C, microSD and LilyGo button positions
- Real battery size (assumed 60 × 50 × 6 mm)

### Images

<img width="1022" height="692" alt="image" src="https://github.com/user-attachments/assets/eab6d722-e9ae-4315-b2ca-74cef5a6b1ca" />
<img width="1170" height="753" alt="image" src="https://github.com/user-attachments/assets/a2d1f387-2672-4a5b-b49b-78fbb14a3d21" />
<img width="1157" height="714" alt="image" src="https://github.com/user-attachments/assets/e4e2a22a-09ec-456e-9b69-2995b8165f97" />
<img width="1125" height="707" alt="image" src="https://github.com/user-attachments/assets/8316e773-5d09-4994-95fb-47c033008cb5" />


### Next steps
- [ ] Firmware: EPUB reader, button handling, deep sleep, haptic buzz
