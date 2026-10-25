// Runs the real main.cpp on a PC with fake hardware and scripted button presses.
//   see run_sim.sh
#include <map>
#include <vector>
#include "Arduino.h"
#include "SD.h"
#include "WiFi.h"
#include "WebServer.h"
#include "epd_driver.h"

uint32_t g_millis = 0;
int g_pins[64];
int g_motorPulses = 0;
int g_wakeCause = 0;
int g_batteryMv = 1950;          // x2 divider -> 3.9 V
FakeSerial Serial; FakeSPI SPI; FakeSD SD; FakeWiFi WiFi;
std::map<std::string, std::string> g_prefs;
std::string sdRoot() { return getenv("SDROOT") ? getenv("SDROOT") : "/tmp/sdcard"; }
int analogReadMilliVolts(int) { return g_batteryMv; }

// ---- fake e-paper
struct Line { int x, y; std::string s; };
std::vector<Line> drawn, screen;
int refreshes = 0;
void epd_init() {} void epd_poweron() {} void epd_poweroff() {} void epd_poweroff_all() {}
void epd_clear() {}
void epd_draw_hline(int, int, int, uint8_t, uint8_t*) {}
static int glyphs(const char* s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }
void get_text_bounds(const GFXfont*, const char* s, int32_t*, int32_t*, int32_t*, int32_t*, int32_t* w, int32_t* h, void*) {
  *w = glyphs(s) * 16; *h = 40;               // pretend every character is 16 px wide
}
void writeln(GFXfont*, const char* s, int32_t* x, int32_t* y, uint8_t*) {
  for (const char* p = s; *p; p++) if ((unsigned char)*p < 0x20) { printf("!! control char 0x%02x drawn\n", *p); exit(1); }
  if (*x + glyphs(s) * 16 > EPD_WIDTH) { printf("!! line too wide: '%s'\n", s); exit(1); }
  drawn.push_back({*x, *y, s});
}
void epd_draw_grayscale_image(Rect_t, uint8_t*) { screen = drawn; drawn.clear(); refreshes++; }

// ---- things from main.cpp
extern int pageNo, sel, mode;
extern std::vector<uint32_t> pages, chapters;
extern String bookTitle;
extern WebServer server;
void setup(); void loop(); void handleUpload(); void handleRoot();
int totalPages();

void showScreen(const char* why) {
  printf("\n--- %s  (refresh #%d, buzzes so far %d) ---\n", why, refreshes, g_motorPulses);
  for (auto& l : screen) printf("  %s%s\n", l.x > 60 ? "    " : "", l.s.c_str());
}
bool asleep = false;
void run(uint32_t ms) {
  uint32_t end = g_millis + ms;
  try { while (g_millis < end) loop(); } catch (DeepSleep&) { asleep = true; printf("\n*** DEEP SLEEP at t=%us ***\n", g_millis / 1000); }
}
void press(int pin, uint32_t ms) { g_pins[pin] = LOW; run(ms); g_pins[pin] = HIGH; run(60); }
void boot(bool wake) {
  asleep = false; g_wakeCause = wake ? 2 : 0;
  try { setup(); } catch (DeepSleep&) { asleep = true; printf("*** slept during setup ***\n"); }
}

int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } else printf("  ok: %s\n", #c); } while (0)

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  for (auto& p : g_pins) p = HIGH;
  const int MENU = 10, PREV = 39, NEXT = 48;
  enum { READING, LIBRARY, UPLOAD, MESSAGE };

  boot(false);
  showScreen("first boot");
  CHECK(mode == LIBRARY);

  // Library rows (alphabetical): Lighthouse.epub, Notes.txt, broken.epub, Vibration
  press(MENU, 100);                                   // open Lighthouse.epub
  showScreen("opened the EPUB");
  CHECK(mode == READING);
  CHECK(bookTitle == "The Lighthouse Keeper\u2019s Daughter");
  CHECK(chapters.size() == 5);
  CHECK(pageNo == 0);
  printf("  book has %d pages\n", totalPages());

  int b = g_motorPulses;
  press(NEXT, 100); press(NEXT, 100);
  CHECK(pageNo == 2);
  CHECK(g_motorPulses - b == 2);                      // one tick per page
  showScreen("page 3");

  b = g_motorPulses;
  press(NEXT, 900);                                   // hold -> next chapter
  showScreen("held NEXT: chapter 2");
  CHECK(g_motorPulses - b == 2);                      // double buzz
  int ch2 = pageNo;
  CHECK(screen.size() > 0 && screen[0].s == "Chapter 2");

  press(NEXT, 100);
  press(PREV, 900);                                   // back to the start of chapter 2
  CHECK(pageNo == ch2);
  press(PREV, 900);                                   // then to chapter 1
  CHECK(pageNo == 0);
  b = g_motorPulses;
  press(PREV, 100);                                   // can't go before page 1
  CHECK(pageNo == 0 && g_motorPulses - b == 3);       // error buzz

  // every chapter must start at the top of a page
  for (size_t k = 0; k < chapters.size(); k++) {
    bool found = false;
    for (size_t p = 0; p < pages.size(); p++) if (pages[p] == chapters[k] || pages[p] + 1 == chapters[k]) found = true;
    CHECK(found);
  }

  // go to page 5 then sleep with a 3.5 s MENU hold
  for (int i = 0; i < 4; i++) press(NEXT, 100);
  int before = refreshes;
  b = g_motorPulses;
  g_pins[MENU] = LOW; run(3500);
  CHECK(g_motorPulses - b == 2);                      // two CUE pulses while holding
  g_pins[MENU] = HIGH; run(100);
  CHECK(asleep);

  // wake with MENU: same page, no redraw
  g_pins[MENU] = LOW;
  pageNo = -1;
  boot(true);
  g_pins[MENU] = HIGH; run(100);
  CHECK(mode == READING && pageNo == 4);
  CHECK(refreshes == before + 0 || refreshes == before);   // nothing redrawn
  CHECK(mode == READING);                             // the wake press didn't open the library

  // MENU -> library, move to Notes.txt, open it
  press(MENU, 100);
  CHECK(mode == LIBRARY);
  press(NEXT, 100);
  press(MENU, 100);
  showScreen("opened the .txt");
  CHECK(mode == READING && chapters.empty());
  press(NEXT, 900);                                   // .txt: hold = 10 pages
  CHECK(pageNo == min(10, totalPages() - 1));

  // broken EPUB -> friendly error, any button -> library
  press(MENU, 100); press(NEXT, 100);
  press(MENU, 100);
  showScreen("broken epub");
  CHECK(mode == MESSAGE);
  press(NEXT, 100);
  CHECK(mode == LIBRARY);

  // vibration row
  press(NEXT, 100);
  CHECK(sel == 3);
  press(MENU, 100);
  showScreen("vibration off");
  CHECK(g_prefs["haptics"] == "0");
  b = g_motorPulses; press(PREV, 100); CHECK(g_motorPulses == b);   // silent now
  press(NEXT, 100); press(MENU, 100);                                // back on
  CHECK(g_prefs["haptics"] == "1");

  // Wi-Fi upload: hold MENU 1.5 s, send a book in chunks
  g_pins[MENU] = LOW; run(1500); g_pins[MENU] = HIGH; run(100);
  showScreen("upload mode");
  CHECK(mode == UPLOAD);
  FILE* src = fopen((sdRoot() + "/Lighthouse.epub").c_str(), "rb");
  auto& up = server.upload();
  up.filename = "C:\\fakepath\\New Book.epub"; up.filename = "New:Book.epub";
  up.status = UPLOAD_FILE_START; handleUpload();
  while ((up.currentSize = fread(up.buf, 1, sizeof(up.buf), src)) > 0) { up.status = UPLOAD_FILE_WRITE; handleUpload(); }
  up.status = UPLOAD_FILE_END; handleUpload(); fclose(src);
  CHECK(SD.exists("/New_Book.epub"));
  up.filename = "virus.exe"; up.status = UPLOAD_FILE_START; handleUpload();
  CHECK(!SD.exists("/virus.exe"));
  handleRoot();
  CHECK(server.lastBody.find("Only .epub and .txt") != std::string::npos);
  CHECK(server.lastBody.find("New_Book.epub") != std::string::npos);
  CHECK(server.lastBody.find("Lighthouse Keeper") != std::string::npos);   // real title shown
  press(MENU, 100);
  CHECK(mode == LIBRARY);
  showScreen("library after upload");

  // low battery while reading -> message + sleep
  CHECK(sel == 3);
  press(PREV, 100); press(PREV, 100); press(PREV, 100);
  CHECK(sel == 0);
  press(MENU, 100);
  CHECK(mode == READING && pageNo == 4);              // bookmark kept
  g_batteryMv = 1640;                                 // 3.28 V
  press(NEXT, 100);
  showScreen("low battery");
  CHECK(asleep);
  g_batteryMv = 1950;

  // idle for 10 minutes -> sleep
  boot(false);
  run(11 * 60 * 1000);
  CHECK(asleep);

  printf("\n%s (%d failures)\n", fails ? "SIMULATION FAILED" : "SIMULATION PASSED", fails);
  return fails != 0;
}
