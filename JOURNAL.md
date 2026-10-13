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
