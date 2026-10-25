// Fake e-paper: remembers the text drawn, prints it on refresh.
#pragma once
#include "Arduino.h"
#define EPD_WIDTH 960
#define EPD_HEIGHT 540
typedef struct { uint32_t first, last, offset; } UnicodeInterval;
typedef struct { uint8_t* bitmap; void* glyph; UnicodeInterval* intervals; uint32_t interval_count; bool compressed; uint8_t advance_y; int ascender; int descender; } GFXfont;
struct Rect_t { int x, y, width, height; };
inline Rect_t epd_full_screen() { return {0, 0, EPD_WIDTH, EPD_HEIGHT}; }
void epd_init(); void epd_poweron(); void epd_poweroff(); void epd_poweroff_all(); void epd_clear();
void epd_draw_grayscale_image(Rect_t, uint8_t*);
void epd_draw_hline(int x, int y, int len, uint8_t color, uint8_t* fb);
void writeln(GFXfont* f, const char* s, int32_t* x, int32_t* y, uint8_t* fb);
void get_text_bounds(const GFXfont* f, const char* s, int32_t* x, int32_t* y, int32_t* x1, int32_t* y1, int32_t* w, int32_t* h, void*);
