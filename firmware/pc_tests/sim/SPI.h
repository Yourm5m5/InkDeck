#pragma once
struct FakeSPI { void begin(int, int, int, int) {} };
extern FakeSPI SPI;
