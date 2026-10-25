## Day 1 – 14/10/2026: Planning InkDeck
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

### Still to check (placeholders)
- Exact position of the e-paper active area on the LilyGo
- USB-C, microSD and LilyGo button positions
- Real battery size (assumed 60 × 50 × 6 mm)

### Images

<img width="1022" height="692" alt="image" src="https://github.com/user-attachments/assets/eab6d722-e9ae-4315-b2ca-74cef5a6b1ca" />
<img width="1170" height="753" alt="image" src="https://github.com/user-attachments/assets/a2d1f387-2672-4a5b-b49b-78fbb14a3d21" />
<img width="1157" height="714" alt="image" src="https://github.com/user-attachments/assets/e4e2a22a-09ec-456e-9b69-2995b8165f97" />
<img width="1125" height="707" alt="image" src="https://github.com/user-attachments/assets/8316e773-5d09-4994-95fb-47c033008cb5" />
<img width="712" height="619" alt="image" src="https://github.com/user-attachments/assets/4c9166d9-3aa8-4f84-b89e-d44469437411" />


### Next steps
- [ ] Firmware: EPUB reader, button handling, deep sleep, haptic buzz

## Day 4-12 – 25/10/2026: InkDeck firmware
**Time spent:** ~ 14 hours

### What I did
- Wrote the firmware in PlatformIO (Arduino framework) with the LilyGo-EPD47 e-paper library
- Split the code into small files so it's easier to follow:
  - `config.h` – every pin and setting in one place
  - `buttons.h` – debounced buttons with tap / hold / long press
  - `haptic.h` – vibration patterns
  - `epub.cpp` – turns an EPUB into plain text
  - `main.cpp` – screens, page layout, Wi-Fi upload, sleep and the main loop
- **EPUB reader**
  - An EPUB is a ZIP file full of web pages. The firmware reads the ZIP's file list,
    finds `content.opf` (title + chapter order), unzips each chapter, removes the
    HTML tags and saves plain text to the SD card
  - Uses the unzip function built into the ESP32-S3's ROM, so no extra library
  - Only converts once per book; after that it opens instantly
  - `<p>` = indented paragraph, headings get blank lines, `&amp;` etc. become real characters
  - Every chapter starts on a new page
- **Pages**
  - Greedy word-wrap: add words to a line until the next one doesn't fit
  - Page start positions are saved on the SD card, so it can show "57 / 312" and go back
  - Footer shows the title, chapter, page and battery: `Ch 3/27   57 / 312   70%`
  - Bookmark saved on every page turn
- **Buttons**
  - A press only counts once the button has been stable for 25 ms (stops double page turns)
  - Hold NEXT / PREV 0.7 s = jump a chapter
  - MENU: tap = library, hold 1 s = Wi-Fi upload, hold 3 s = sleep
- **Haptic buzz** – a different feel for every action
- **Deep sleep** – after 10 min idle, a 3 s MENU hold, or a nearly flat battery.
  MENU (IO10) wakes it on the same page
- **Wi-Fi upload** – the reader makes its own hotspot; a phone can add `.epub` / `.txt`
  books or delete them
- Wrote `FIRMWARE.md` explaining every part with code snippets and diagrams

### Controls
| Button | Reading | Library |
|---|---|---|
| NEXT / PREV | Next / previous page | Move down / up |
| Hold NEXT / PREV (0.7 s) | Next / previous chapter | Move 5 rows |
| MENU | Open the library | Open the book / vibration on-off |
| Hold MENU 1 s | Wi-Fi upload | Wi-Fi upload |
| Hold MENU 3 s | Sleep | Sleep |

### Haptic patterns
| Pattern | Meaning |
|---|---|
| 1 short buzz | Page turn |
| 2 buzzes | Chapter jump |
| 1 buzz at 1 s and 3 s while holding MENU | Shows which action you'll get |
| 3 quick buzzes | Can't go further (first / last page) or book can't open |
| 1 long buzz | Going to sleep |

### Code snippets
Debouncing – a new state only counts after 25 ms without changing:
```cpp
if (r != raw_) { raw_ = r; changedAt_ = now; }    // still bouncing: restart the timer
if (raw_ != stable_ && now - changedAt_ >= DEBOUNCE_MS) stable_ = raw_;
```

Unzipping a chapter with the ESP32-S3's built-in ROM function:
```cpp
tinfl_status st = tinfl_decompress(d, in, &inBytes, out, out, &outBytes,
                                   TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
```

Going to sleep:
```cpp
while (digitalRead(PIN_BTN_MENU) == LOW) delay(10);   // never sleep while MENU is held
esp_sleep_enable_ext0_wakeup(GPIO_NUM_10, 0);         // MENU wakes it up
esp_deep_sleep_start();
```

### Problems I solved
- Sleeping while MENU was still held would wake it straight away (wake = MENU LOW),
  so MENU now acts when you let go
- The wake-up press was also opening the library → buttons already held at start-up
  are ignored until released
- The font doesn't have curly quotes or long dashes → the firmware checks the font
  and swaps them for `"` and `--`
- Shop-bought books with DRM can't be read → shows a "This book has DRM" message instead of crashing
- IO45 is a strapping pin → the motor pin is set LOW as the very first thing at boot
<img width="0" height="0" alt="image" src="https://github.com/user-attachments/assets/1cdd64ca-6c80-4e4b-89fd-7ab4e5b3adec" />

### Testing
- I don't have the parts yet, so the firmware was tested on a computer:
  - EPUB converter: 5 test books (including a DRM book and a fake EPUB) – all passed
  - Simulation of the whole `main.cpp` with fake buttons, screen and SD card:
    opening books, page turns, chapter jumps, sleep / wake, upload, low battery – 43 checks passed
- Still needs testing on the real LilyGo

### Next steps
- [ ] Ship and complete readme

*AI note: I used Claude to help write and test the firmware and the documentation.*
