// =============================================================================
//  InkDeck – DIY e-reader firmware
// =============================================================================
//  Board    : LilyGo T5 4.7" E-Paper S3  (ESP32-S3, 16 MB flash, 8 MB PSRAM)
//  Display  : 4.7" ED047TC1 e-paper, 960 x 540, 16 grey levels
//  Grip PCB : InkDeck grip board (KiCad) – buttons + vibration motor, see config.h
//
//  What it does
//    - Reads .epub and .txt books from the microSD card
//    - NEXT / PREV          turn the page (short buzz)
//    - hold NEXT / PREV     jump to the next / previous chapter (double buzz)
//    - MENU                 library  (pick a book, switch vibration on/off)
//    - hold MENU 1 s        Wi-Fi upload mode (add/delete books from a phone)
//    - hold MENU 3 s        sleep now      (10 min idle also sleeps)
//    - MENU while asleep    wake up on the same page
//
//  Files
//    config.h    pins + every setting
//    haptic.h    vibration patterns
//    buttons.h   debounced click / hold / long-press detection
//    epub.h/.cpp EPUB -> plain text converter (no Arduino code, tested on a PC)
//    main.cpp    this file: screens, books, pages, Wi-Fi, sleep, main loop
//
//  This file, top to bottom
//    1. Includes + global state
//    2. Helpers          – text drawing, battery, screen refresh, file names
//    3. EPUB support     – ROM inflate, font check, SD adapters, cached conversion
//    4. Text layout      – one page of word-wrapped text
//    5. Page index       – page start offsets cached on the SD card
//    6. Books            – scanning, opening, bookmarks, chapters
//    7. Screens          – reading page, library, messages
//    8. Wi-Fi upload     – hotspot + web page to add / delete books
//    9. Sleep            – deep sleep, wake on MENU
//   10. setup() / loop() – start-up and the main state machine
//
//  See FIRMWARE.md for diagrams and a full explanation.
// =============================================================================

// ---------------------------------------------------------------------------
// 1. INCLUDES + GLOBAL STATE
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>       // small key/value store in flash (survives sleep + reboot)
#include <vector>
#include "driver/rtc_io.h"     // keep the MENU pull-up on during deep sleep
#include "epd_driver.h"        // LilyGo e-paper driver
#include "firasans.h"          // FiraSans font from the driver library

// The ESP32-S3 has a DEFLATE decoder (miniz "tinfl") built into its ROM, so
// unzipping EPUBs costs no extra flash. The header lives in one of these places
// depending on the ESP-IDF version.
#if __has_include("esp32s3/rom/miniz.h")
#include "esp32s3/rom/miniz.h"
#else
#include "rom/miniz.h"
#endif

#include "config.h"
#include "haptic.h"
#include "buttons.h"
#include "epub.h"

const GFXfont* font = (const GFXfont*)&FiraSans;

// Framebuffer: 4 bits per pixel (16 greys) -> 960*540/2 = 259 KB, so it lives in PSRAM.
// Everything is drawn here first, then the whole buffer is pushed to the panel at once.
uint8_t* fb = nullptr;

Preferences prefs;           // remembers the open book + vibration setting
WebServer   server(80);      // only used in upload mode
File        uploadFile;      // file currently arriving over Wi-Fi
String      uploadMsg;       // result shown on the web page after an upload

Button btnMenu(PIN_BTN_MENU, MENU_BUTTON);
Button btnPrev(PIN_BTN_PREV, PAGE_BUTTON);
Button btnNext(PIN_BTN_NEXT, PAGE_BUTTON);

// The reader is always in exactly one of these modes (state diagram in FIRMWARE.md)
enum Mode { READING, LIBRARY, UPLOAD, MESSAGE };
Mode mode = LIBRARY;

// The open book
String   bookSrc;              // file in the library, e.g. "/Dracula.epub"
String   textPath;             // plain text actually paged: the .txt itself, or the converted EPUB
String   bookTitle;            // shown in the footer
uint32_t bookSize = 0;         // bytes in textPath
std::vector<uint32_t> pages;   // byte offset where each page starts; last entry = end of text
std::vector<uint32_t> chapters;// byte offset where each chapter starts (EPUB only)
int      pageNo = 0;           // current page (0-based)

// The library
std::vector<String> books;     // every .txt / .epub in the card's root folder
std::vector<String> titles;    // what to show for each (EPUB title once converted)
int      sel = 0;              // highlighted row (the last row is the vibration setting)
uint32_t lastActivity = 0;     // millis() of the last button press, for auto-sleep
int      lastBattery = -1;     // % from the last screen update (-1 = unknown / USB only)

// ---------------------------------------------------------------------------
// 2. HELPERS
// ---------------------------------------------------------------------------
// Pixel width of a string in the reading font (used by the word-wrap).
int textWidth(const String& s) {
  int32_t x = 0, y = 0, x1, y1, w, h;
  get_text_bounds(font, s.c_str(), &x, &y, &x1, &y1, &w, &h, NULL);
  return w;
}

// Draw text into the framebuffer. y is the text BASELINE, not the top.
void drawText(int x, int y, const String& s) {
  int32_t cx = x, cy = y;
  writeln((GFXfont*)font, s.c_str(), &cx, &cy, fb);
}

// Shorten text with "..." until it fits in maxW pixels (for long titles).
String fitText(String s, int maxW) {
  if (textWidth(s) <= maxW) return s;
  while (s.length() > 1 && textWidth(s + "...") > maxW) {
    int k = s.length() - 1;                          // remove one whole character:
    while (k > 0 && (s[k] & 0xC0) == 0x80) k--;      // UTF-8 letters like "é" are 2+ bytes
    s.remove(k);
  }
  return s + "...";
}

// 0xFF = both pixels in the byte white.
void clearFb() { memset(fb, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2); }

// Battery percentage from IO14 (through a 1:2 divider).
// The divider is only powered while the EPD power rail is on, so this switches
// the rail on just for the reading.   4.15 V = 100 %, 3.3 V = 0 %.
// Returns -1 if there's no sensible reading (e.g. no battery plugged in).
int batteryPercent() {
  epd_poweron();
  delay(2);
  float v = analogReadMilliVolts(PIN_BAT_ADC) * 2 / 1000.0;   // x2 undoes the divider
  epd_poweroff();
  if (v < 2.5) return -1;
  return constrain((int)((v - 3.3) / (4.15 - 3.3) * 100), 0, 100);
}

// Push the framebuffer to the panel. epd_clear() flashes the screen to erase
// the old image (stops "ghosting"), then the new image is drawn. Power is only
// on during the update – e-paper keeps its picture with zero power.
void showFb() {
  epd_poweron();
  epd_clear();
  epd_draw_grayscale_image(epd_full_screen(), fb);
  epd_poweroff();
}

// "/books/Dracula.epub" -> "Dracula.epub"
String fileName(const String& path) { return path.substring(path.lastIndexOf('/') + 1); }

// "/books/Dracula.epub" -> "Dracula"
String baseName(const String& path) {
  String n = fileName(path);
  int dot = n.lastIndexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

bool isEpub(const String& p) { String l = p; l.toLowerCase(); return l.endsWith(".epub"); }
bool isTxt(const String& p)  { String l = p; l.toLowerCase(); return l.endsWith(".txt"); }

// Every book gets its own cache files in a hidden folder:
//   /.inkdeck/Dracula.epub.txt   converted text   (EPUB only)
//   /.inkdeck/Dracula.epub.meta  title + chapters (EPUB only)
//   /.inkdeck/Dracula.epub.idx   page index
//   /.inkdeck/Dracula.epub.pos   bookmark
String cachePath(const String& src, const char* ext) { return "/.inkdeck/" + fileName(src) + ext; }

void removeCaches(const String& src) {
  for (const char* e : {".txt", ".meta", ".idx", ".pos"}) SD.remove(cachePath(src, e));
}

// ---------------------------------------------------------------------------
// 3. EPUB SUPPORT
// ---------------------------------------------------------------------------
// epub.cpp doesn't know about SD cards or the ESP32, so we hand it these:

// (a) Unzip with the ROM's tinfl. The whole compressed chapter is in RAM and we
//     know its unpacked size from the ZIP directory, so it's a single call.
bool romInflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
  tinfl_decompressor* d = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));  // ~11 KB
  if (!d) return false;
  tinfl_init(d);
  size_t inBytes = inLen, outBytes = outLen;
  tinfl_status st = tinfl_decompress(d, in, &inBytes, out, out, &outBytes,
                                     TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(d);
  return st == TINFL_STATUS_DONE && outBytes == outLen;
}

// (b) Does FiraSans have this character? The font stores its characters as
//     ranges ("intervals"). Anything missing gets swapped by epub.cpp, e.g.
//     curly quotes -> straight quotes, so no blank boxes on the page.
bool fontHasGlyph(uint32_t cp) {
  for (uint32_t i = 0; i < font->interval_count; i++)
    if (cp >= font->intervals[i].first && cp <= font->intervals[i].last) return true;
  return false;
}

// (c) SD-card File <-> the converter's Source / Sink
struct SdSource : epub::Source {
  File& f;
  explicit SdSource(File& file) : f(file) {}
  uint32_t size() override { return f.size(); }
  bool readAt(uint32_t off, uint8_t* dst, uint32_t len) override {
    return f.seek(off) && f.read(dst, len) == (int)len;
  }
};
struct SdSink : epub::Sink {
  File& f;
  explicit SdSink(File& file) : f(file) {}
  bool write(const char* d, size_t n) override { return f.write((const uint8_t*)d, n) == n; }
};

// (d) The converted text is cached, so each EPUB is only converted once.
// .meta format:  [META_VERSION][epub size][text size][chapter count][offsets...][title length][title]
// If the EPUB is replaced (size changes) or the format changes, it's redone.
bool loadMeta(const String& src, uint32_t epubSize, String* title, std::vector<uint32_t>* chs) {
  File f = SD.open(cachePath(src, ".meta"));
  if (!f) return false;
  uint32_t h[4];
  bool ok = f.read((uint8_t*)h, sizeof(h)) == sizeof(h) && h[0] == META_VERSION && h[1] == epubSize;
  if (ok && chs) {
    chs->resize(h[3]);
    ok = f.read((uint8_t*)chs->data(), h[3] * 4) == (int)(h[3] * 4);
  } else if (ok) {
    f.seek(16 + h[3] * 4);
  }
  if (ok && title) {
    uint32_t len = 0;
    f.read((uint8_t*)&len, 4);
    std::vector<char> t(len + 1, 0);
    f.read((uint8_t*)t.data(), len);
    *title = String(t.data());
  }
  f.close();
  if (ok) {                                   // the text file must still be there too
    File t = SD.open(cachePath(src, ".txt"));
    ok = t && t.size() == h[2];
    if (t) t.close();
  }
  return ok;
}

void saveMeta(const String& src, uint32_t epubSize, const epub::Result& r) {
  File f = SD.open(cachePath(src, ".meta"), FILE_WRITE);
  if (!f) return;
  uint32_t h[4] = { META_VERSION, epubSize, r.textBytes, (uint32_t)r.chapters.size() };
  f.write((uint8_t*)h, sizeof(h));
  f.write((uint8_t*)r.chapters.data(), r.chapters.size() * 4);
  uint32_t len = r.title.size();
  f.write((uint8_t*)&len, 4);
  f.write((const uint8_t*)r.title.data(), len);
  f.close();
}

void showMessage(const String& a, const String& b = "", const String& c = "");   // section 7
void lowBatterySleep();                                                            // section 9

// Convert src -> /.inkdeck/<name>.txt. Returns "" on success, or an error message.
String convertEpub(const String& src) {
  showMessage("Preparing " + fitText(baseName(src), 700) + " ...",
              "Unpacking the EPUB and splitting it into pages.",
              "This only happens the first time.");
  File in = SD.open(src);
  if (!in) return "Couldn't open the file";
  File out = SD.open(cachePath(src, ".txt"), FILE_WRITE);
  if (!out) { in.close(); return "Couldn't write to the SD card"; }

  SdSource source(in);
  SdSink sink(out);
  epub::Options opt;
  opt.inflate  = romInflate;
  opt.hasGlyph = fontHasGlyph;
  opt.progress = [](int done, int total) { Serial.printf("EPUB: chapter %d / %d\n", done, total); };

  uint32_t t0 = millis();
  epub::Result r = epub::convert(source, sink, opt);
  uint32_t epubSize = in.size();
  in.close();
  out.close();
  if (!r.ok) {
    SD.remove(cachePath(src, ".txt"));
    return String(r.error.c_str());
  }
  saveMeta(src, epubSize, r);
  Serial.printf("EPUB: '%s', %u chapters, %u bytes of text, %u ms\n",
                r.title.c_str(), (unsigned)r.chapters.size(), (unsigned)r.textBytes, (unsigned)(millis() - t0));
  return "";
}

// ---------------------------------------------------------------------------
// 4. TEXT LAYOUT
// ---------------------------------------------------------------------------
// Fills ONE page starting at byte offset `start` of textPath and returns the
// offset where the next page begins.
//   draw = true  -> also draws the lines into the framebuffer
//   draw = false -> only measures (used to build the page index)
// The same function does both, so page breaks always match.
//
// Greedy word-wrap: add words to the line while they fit; when one doesn't,
// finish the line and start the next with that word. Special characters:
//   '\n'  finishes the line (paragraph end; two in a row = blank line)
//   '\t'  at the start of a line -> indent it INDENT_PX (paragraph start)
//   '\f'  chapter start -> the page ends here so the chapter starts on a new page
// Blank lines at the very top of a page are skipped (looks tidier).
uint32_t layoutPage(uint32_t start, bool draw) {
  static char buf[4097];
  File f = SD.open(textPath);
  if (!f) return bookSize;
  f.seek(start);
  int n = f.read((uint8_t*)buf, 4096);
  f.close();
  if (n <= 0) return bookSize;
  buf[n] = 0;
  bool eof = (start + n >= bookSize);

  const int maxW     = EPD_WIDTH - 2 * MARGIN_X;                      // usable width
  const int lineH    = font->advance_y;                               // line spacing from the font
  const int maxLines = (EPD_HEIGHT - MARGIN_TOP - FOOTER_H) / lineH;  // lines per page

  int line = 0, i = 0, consumed = 0;   // consumed = bytes safely placed on this page
  int indent = 0;                      // indent of the line being built
  String cur = "";                     // line being built

  auto flush = [&]() {                 // finish the current line
    if (draw) drawText(MARGIN_X + indent, MARGIN_TOP + lineH * (line + 1), cur);
    cur = ""; indent = 0;
    line++;
  };
  auto topOfPage = [&]() { return line == 0 && cur.length() == 0; };

  while (i < n && line < maxLines) {
    char c = buf[i];
    if (c == '\f') {                                                    // new chapter
      if (topOfPage()) { i++; consumed = i; continue; }                 // already on a fresh page
      if (cur.length()) flush();
      consumed = i;                                                     // next page starts at the '\f'
      break;
    }
    if (c == '\r') { i++; consumed = i; continue; }                     // ignore Windows line endings
    if (c == '\n') {
      if (topOfPage()) { i++; consumed = i; continue; }                 // no blank lines at the top
      flush(); i++; consumed = i; continue;
    }
    if (c == '\t') {                                                    // paragraph indent
      if (cur.length() == 0) indent = INDENT_PX;
      i++; consumed = i; continue;
    }
    if (c == ' ') { i++; consumed = i; continue; }                      // spaces between words

    // find the end of this word
    int j = i;
    while (j < n && buf[j] != ' ' && buf[j] != '\n' && buf[j] != '\r' && buf[j] != '\t' && buf[j] != '\f') j++;
    if (j == n && !eof) break;          // word cut off by the 4 KB buffer -> leave it for the next page

    String word(buf + i, j - i);
    String trial = cur.length() ? cur + " " + word : word;

    if (textWidth(trial) <= maxW - indent) {   // fits -> keep building the line
      cur = trial; i = j; consumed = i;
    } else if (cur.length() == 0) {            // one word wider than the page -> its own line
      cur = word; i = j; consumed = i; flush();
    } else {                                   // line full -> finish it, retry the word on the next line
      flush();
    }
  }
  if (cur.length() && line < maxLines) flush();
  if (consumed == 0) consumed = n;             // safety: always move forward
  return start + consumed;
}

// ---------------------------------------------------------------------------
// 5. PAGE INDEX (cached on the SD card)
// ---------------------------------------------------------------------------
// To show "57 / 312", go BACK a page and jump between chapters we need to know
// where every page starts. Working that out means laying out the whole book
// once (a few seconds), so the result is saved to /.inkdeck/<file>.idx.
//   File format: [INDEX_VERSION][text size][count][offset0][offset1]...
// If the text or the layout rules change, the check fails and it's rebuilt.
bool loadIndex() {
  File f = SD.open(cachePath(bookSrc, ".idx"));
  if (!f) return false;
  uint32_t hdr[3];
  bool ok = f.read((uint8_t*)hdr, sizeof(hdr)) == sizeof(hdr) &&
            hdr[0] == INDEX_VERSION && hdr[1] == bookSize && hdr[2] > 0;
  if (ok) {
    pages.resize(hdr[2]);
    ok = f.read((uint8_t*)pages.data(), hdr[2] * 4) == (int)(hdr[2] * 4);
  }
  f.close();
  return ok;
}

void buildIndex(bool quiet) {
  if (!quiet)   // (a freshly converted EPUB already shows "Preparing...")
    showMessage("Preparing " + fitText(bookTitle, 700) + " ...", "Splitting the book into pages.",
                "This only happens the first time.");
  pages.clear();
  pages.push_back(0);
  uint32_t off = 0;
  while (off < bookSize) {                 // measure page after page until the end
    uint32_t next = layoutPage(off, false);
    if (next <= off) break;
    pages.push_back(next);
    off = next;
  }
  File f = SD.open(cachePath(bookSrc, ".idx"), FILE_WRITE);
  if (!f) return;
  uint32_t hdr[3] = { INDEX_VERSION, bookSize, (uint32_t)pages.size() };
  f.write((uint8_t*)hdr, sizeof(hdr));
  f.write((uint8_t*)pages.data(), pages.size() * 4);
  f.close();
}

int totalPages() { return max(1, (int)pages.size() - 1); }

// ---------------------------------------------------------------------------
// 6. BOOKS
// ---------------------------------------------------------------------------
// Find every .txt / .epub in the card's root folder (hidden files skipped).
void scanBooks() {
  books.clear();
  titles.clear();
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = fileName(f.name());
    if (!f.isDirectory() && !name.startsWith(".") && (isTxt(name) || isEpub(name)))
      books.push_back("/" + name);
  }
  root.close();
  // Show the real title for EPUBs that have been opened before, else the file name.
  for (auto& b : books) {
    String t = baseName(b);
    if (isEpub(b)) {
      File f = SD.open(b);
      uint32_t sz = f ? f.size() : 0;
      if (f) f.close();
      String mt;
      if (loadMeta(b, sz, &mt, nullptr) && mt.length()) t = mt;
    }
    titles.push_back(t);
  }
  sel = constrain(sel, 0, (int)books.size());   // books.size() = the vibration row
}

// Bookmark: each book has its own .pos file, so switching books keeps your place.
void savePosition() {
  File f = SD.open(cachePath(bookSrc, ".pos"), FILE_WRITE);
  if (f) { f.print(pageNo); f.close(); }
  prefs.putString("book", bookSrc);        // which book to reopen after sleep / reboot
}

void loadPosition() {
  pageNo = 0;
  File f = SD.open(cachePath(bookSrc, ".pos"));
  if (f) { pageNo = f.parseInt(); f.close(); }
  pageNo = constrain(pageNo, 0, totalPages() - 1);
}

// Get a book ready to read: convert (EPUB, first time), load/build the page
// index and the bookmark. Doesn't draw the page. Returns "" or an error.
String loadBook(const String& src) {
  bool justConverted = false;
  SD.mkdir("/.inkdeck");
  bookSrc = src;
  chapters.clear();
  if (isEpub(src)) {
    File f = SD.open(src);
    if (!f) return "Couldn't open the file";
    uint32_t sz = f.size();
    f.close();
    if (!loadMeta(src, sz, &bookTitle, &chapters)) {
      justConverted = true;
      String err = convertEpub(src);
      if (err.length()) return err;
      if (!loadMeta(src, sz, &bookTitle, &chapters)) return "Couldn't save the converted book";
    }
    if (bookTitle.length() == 0) bookTitle = baseName(src);
    textPath = cachePath(src, ".txt");
  } else {
    bookTitle = baseName(src);
    textPath = src;
  }
  File t = SD.open(textPath);
  if (!t) return "Couldn't open the text";
  bookSize = t.size();
  t.close();
  if (bookSize == 0) return "This book is empty";
  if (!loadIndex()) buildIndex(justConverted);
  loadPosition();
  return "";
}

// Which page holds byte `off`? (the last page that starts at or before it)
int pageOfOffset(uint32_t off) {
  int lo = 0, hi = totalPages() - 1;
  while (lo < hi) {                        // binary search
    int mid = (lo + hi + 1) / 2;
    if (pages[mid] <= off) lo = mid; else hi = mid - 1;
  }
  return lo;
}

// Which chapter is page p in? (-1 for .txt books)
int chapterOfPage(int p) {
  int ch = -1;
  // A chapter's page starts at its '\f', one byte before the chapter offset.
  for (size_t k = 0; k < chapters.size(); k++) if (chapters[k] <= pages[p] + 1) ch = k;
  return ch;
}

// Hold NEXT/PREV: jump a chapter (EPUB) or 10 pages (.txt).
// PREV works like a music player: first back to the start of this chapter,
// and if you're already there, to the previous one.
int chapterJumpTarget(int dir) {
  if (chapters.empty()) return constrain(pageNo + dir * 10, 0, totalPages() - 1);
  int ch = chapterOfPage(pageNo);
  if (dir > 0) {
    if (ch + 1 >= (int)chapters.size()) return pageNo;          // already in the last chapter
    return pageOfOffset(chapters[ch + 1]);
  }
  int startOfThis = pageOfOffset(chapters[max(ch, 0)]);
  if (pageNo > startOfThis) return startOfThis;
  return ch > 0 ? pageOfOffset(chapters[ch - 1]) : 0;
}

// ---------------------------------------------------------------------------
// 7. SCREENS
// ---------------------------------------------------------------------------
// Footer:  "Dracula" ...................... "Ch 3/27   57 / 312   87%"
void drawFooter(const String& left, const String& right) {
  int y = EPD_HEIGHT - FOOTER_H;
  epd_draw_hline(MARGIN_X, y, EPD_WIDTH - 2 * MARGIN_X, 0, fb);   // divider line
  int bat = lastBattery = batteryPercent();
  String r = right + "   " + (bat < 0 ? String("USB") : String(bat) + "%");
  int rw = textWidth(r);
  drawText(MARGIN_X, y + 45, fitText(left, EPD_WIDTH - 2 * MARGIN_X - rw - 30));
  drawText(EPD_WIDTH - MARGIN_X - rw, y + 45, r);                 // right-aligned
}

void showMessage(const String& a, const String& b, const String& c) {
  clearFb();
  int lh = font->advance_y;
  drawText(MARGIN_X, 180, a);
  if (b.length()) drawText(MARGIN_X, 180 + lh * 2, b);
  if (c.length()) drawText(MARGIN_X, 180 + lh * 3, c);
  showFb();
}

void renderPage() {
  clearFb();
  layoutPage(pages[pageNo], true);
  String right = String(pageNo + 1) + " / " + String(totalPages());
  int ch = chapterOfPage(pageNo);
  if (ch >= 0) right = "Ch " + String(ch + 1) + "/" + String(chapters.size()) + "    " + right;
  drawFooter(bookTitle, right);
  showFb();
  savePosition();                          // bookmark after every page turn
}

// Library: books, then a settings row. "> " marks the selection; the list
// scrolls when it's longer than the screen.
void renderLibrary() {
  clearFb();
  int lineH = font->advance_y;
  drawText(MARGIN_X, MARGIN_TOP + lineH, "Library");
  int rows = books.size() + 1;                                  // + vibration row
  int maxRows = (EPD_HEIGHT - MARGIN_TOP - FOOTER_H) / lineH - 2;
  int first = max(0, sel - maxRows + 1);                        // keep the selection visible
  for (int r = 0; r < maxRows && first + r < rows; r++) {
    int idx = first + r;
    String label;
    if (idx < (int)books.size()) label = titles[idx] + (isEpub(books[idx]) ? "" : "  (txt)");
    else label = String("Vibration: ") + (haptic::enabled() ? "on" : "off");
    drawText(MARGIN_X, MARGIN_TOP + lineH * (r + 3),
             fitText((idx == sel ? "> " : "   ") + label, EPD_WIDTH - 2 * MARGIN_X));
  }
  if (books.empty())
    drawText(MARGIN_X, MARGIN_TOP + lineH * (min(rows, maxRows) + 4), "No books yet - hold MENU 1 s to add some over Wi-Fi.");
  drawFooter("PREV/NEXT move   MENU open", "hold MENU: Wi-Fi");
  showFb();
}

void openBook(const String& src) {
  String err = loadBook(src);
  if (err.length()) {
    haptic::play(ERROR_BUZZ);
    showMessage("Can't open " + fitText(baseName(src), 600), err, "Press any button to go back.");
    mode = MESSAGE;
    return;
  }
  mode = READING;
  renderPage();
}

// ---------------------------------------------------------------------------
// 8. WI-FI UPLOAD MODE
// ---------------------------------------------------------------------------
// The reader becomes its own Wi-Fi hotspot (no home network needed). A phone
// joins it, opens http://192.168.4.1 and uploads .epub / .txt files, which
// are streamed straight onto the SD card in chunks (never held in RAM).
String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;"); s.replace("\"", "&quot;");
  return s;
}

void handleRoot() {
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
                "<title>InkDeck</title></head>"
                "<body style='font-family:sans-serif;max-width:480px;margin:auto;padding:12px'>"
                "<h2>InkDeck library</h2>";
  if (uploadMsg.length()) html += "<p><b>" + htmlEscape(uploadMsg) + "</b></p>";
  html += "<ul>";
  for (size_t i = 0; i < books.size(); i++)
    html += "<li>" + htmlEscape(titles[i]) + " <small>(" + htmlEscape(fileName(books[i])) + ")</small>"
            " <form method='POST' action='/delete' style='display:inline'>"
            "<input type='hidden' name='f' value='" + htmlEscape(fileName(books[i])) + "'>"
            "<button onclick=\"return confirm('Delete this book?')\">Delete</button></form></li>";
  html += "</ul><h3>Add a book (.epub or .txt)</h3>"
          "<form method='POST' action='/upload' enctype='multipart/form-data'>"
          "<input type='file' name='book' accept='.epub,.txt'> <input type='submit' value='Upload'>"
          "</form><p><small>EPUBs with DRM (most shop-bought books) can't be read. "
          "Free books: Project Gutenberg, Standard Ebooks.</small></p></body></html>";
  server.send(200, "text/html", html);
}

// Called repeatedly by WebServer while a file arrives:
//   START -> check the name + open the file,  WRITE -> append a chunk,  END -> close
void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    String fn = fileName(up.filename);                 // some browsers send a whole path
    for (const char* bad : {"\\", ":", "*", "?", "\"", "<", ">", "|"}) fn.replace(bad, "_");
    if (fn.startsWith(".") || !(isEpub(fn) || isTxt(fn))) {
      uploadMsg = "Only .epub and .txt files can be added.";
      return;
    }
    String path = "/" + fn;
    SD.remove(path);                                   // replacing a book: start fresh
    removeCaches(path);
    uploadFile = SD.open(path, FILE_WRITE);
    uploadMsg = uploadFile ? "Added " + fn : "Couldn't write to the SD card.";
  } else if (up.status == UPLOAD_FILE_WRITE && uploadFile) {
    uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END && uploadFile) {
    uploadFile.close();
    scanBooks();
  } else if (up.status == UPLOAD_FILE_ABORTED && uploadFile) {
    String path = "/" + fileName(uploadFile.name());
    uploadFile.close();
    SD.remove(path);                                   // don't keep half a book
    uploadMsg = "Upload interrupted.";
  }
  lastActivity = millis();                             // uploading counts as activity
}

void handleDelete() {
  String path = "/" + fileName(server.arg("f"));
  if ((isEpub(path) || isTxt(path)) && SD.exists(path)) {
    SD.remove(path);
    removeCaches(path);
    if (prefs.getString("book", "") == path) prefs.remove("book");
    uploadMsg = "Deleted " + fileName(path);
    scanBooks();
  }
  server.sendHeader("Location", "/");
  server.send(303);
  lastActivity = millis();
}

void startUpload() {
  mode = UPLOAD;
  uploadMsg = "";
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASS);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST,
            []() { server.sendHeader("Location", "/"); server.send(303); },   // back to the list
            handleUpload);
  server.on("/delete", HTTP_POST, handleDelete);
  server.begin();

  clearFb();
  int lh = font->advance_y;
  drawText(MARGIN_X, 80, "Wi-Fi upload mode");
  drawText(MARGIN_X, 80 + lh * 2, String("1. Join Wi-Fi: ") + AP_NAME);
  drawText(MARGIN_X, 80 + lh * 3, String("    Password: ") + AP_PASS);
  drawText(MARGIN_X, 80 + lh * 4, "2. Open http://" + WiFi.softAPIP().toString());
  drawText(MARGIN_X, 80 + lh * 5, "3. Add .epub or .txt books");
  drawText(MARGIN_X, 80 + lh * 7, "Press MENU when done.");
  showFb();
}

void stopUpload() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);                 // radio off = big battery saving
  scanBooks();
  mode = LIBRARY;
  renderLibrary();
}

// ---------------------------------------------------------------------------
// 9. SLEEP
// ---------------------------------------------------------------------------
// Deep sleep turns almost everything off (microamps). The e-paper keeps showing
// the last page with no power, so it still looks like a book.
// Waking: "ext0" watches ONE RTC pin – MENU on IO10. Pressing it pulls IO10
// LOW and the chip restarts from setup(). The 10k pull-up on the grip PCB keeps
// IO10 HIGH while asleep; the internal RTC pull-up is switched on as a backup.
void goToSleep() {
  haptic::play(SLEEP_BUZZ);                           // long buzz = "going to sleep"
  if (mode == UPLOAD) { server.stop(); WiFi.mode(WIFI_OFF); }
  while (digitalRead(PIN_BTN_MENU) == LOW) delay(10); // never sleep with MENU held (it would wake at once)
  digitalWrite(PIN_MOTOR, LOW);                       // motor off (PCB pull-down keeps it off while asleep)
  epd_poweroff_all();                                 // cut power to the display driver completely
  rtc_gpio_pullup_en(GPIO_NUM_10);
  rtc_gpio_pulldown_dis(GPIO_NUM_10);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_10, 0);       // wake when MENU = LOW
  esp_deep_sleep_start();                             // never returns
}

// Nearly flat: say so on the screen (it stays visible while off), then sleep.
void lowBatterySleep() {
  showMessage("Battery low", "Please charge InkDeck (USB-C on the left edge).",
              "Press MENU after charging.");
  goToSleep();
}

// ---------------------------------------------------------------------------
// 10. SETUP / LOOP
// ---------------------------------------------------------------------------
void setup() {
  haptic::begin();                     // motor off immediately at boot
  btnMenu.begin(false);                // external 10k pull-up (works in deep sleep)
  btnPrev.begin(true);                 // internal pull-ups are enough while awake
  btnNext.begin(true);
  Serial.begin(115200);

  fb = (uint8_t*)ps_calloc(1, EPD_WIDTH * EPD_HEIGHT / 2);   // framebuffer in PSRAM
  epd_init();
  if (!fb) { Serial.println("No PSRAM - check board settings"); while (true) delay(1000); }

  prefs.begin("inkdeck", false);
  haptic::enabled() = prefs.getBool("haptics", HAPTICS_DEFAULT);

  int bat = batteryPercent();
  if (bat >= 0 && bat <= LOW_BATTERY) lowBatterySleep();

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI)) {         // no card = nothing to read
    showMessage("No microSD card found.", "Insert a card (FAT32) and press MENU.");
    goToSleep();                       // MENU wakes it and tries again
  }
  scanBooks();

  // Woken from deep sleep by MENU? Then the page is still on the e-paper.
  bool wokeByButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
  String last = prefs.getString("book", "");

  if (last.length() && SD.exists(last) && loadBook(last) == "") {
    mode = READING;
    if (wokeByButton) haptic::play(TICK);   // skip the redraw: saves a flash + power
    else renderPage();
  } else {
    mode = LIBRARY;
    renderLibrary();
  }
  lastActivity = millis();             // the wake press is ignored by btnMenu until released
}

// Main state machine. Each mode decides what the buttons do:
//
//            MENU                    MENU (on a book)
//   READING ──────▶ LIBRARY ─────────────────────────▶ READING
//      │  ▲            │   ╲ (error) ──▶ MESSAGE ── any button ──▶ LIBRARY
//      │  │ MENU       │ hold MENU 1 s
//      │  └── UPLOAD ◀─┘ (also from READING)
//      │
//      └── hold MENU 3 s, 10 min idle or low battery ──▶ DEEP SLEEP ── MENU ──▶ setup()
void loop() {
  BtnEvent menu = btnMenu.update();
  BtnEvent next = btnNext.update();
  BtnEvent prev = btnPrev.update();
  if (menu || next || prev || btnMenu.isDown() || btnNext.isDown() || btnPrev.isDown())
    lastActivity = millis();

  if (menu == BTN_VLONG) goToSleep();          // works in every mode

  switch (mode) {
    case READING:
      if (next == BTN_CLICK) {
        if (pageNo < totalPages() - 1) { haptic::play(TICK); pageNo++; renderPage(); }
        else haptic::play(ERROR_BUZZ);                      // last page
      }
      if (prev == BTN_CLICK) {
        if (pageNo > 0) { haptic::play(TICK); pageNo--; renderPage(); }
        else haptic::play(ERROR_BUZZ);                      // first page
      }
      if (next == BTN_HOLD || prev == BTN_HOLD) {
        int target = chapterJumpTarget(next == BTN_HOLD ? +1 : -1);
        if (target != pageNo) { haptic::play(DOUBLE); pageNo = target; renderPage(); }
        else haptic::play(ERROR_BUZZ);
      }
      if (menu == BTN_CLICK) { haptic::play(TICK); mode = LIBRARY; scanBooks(); renderLibrary(); }
      if (menu == BTN_LONG)  { startUpload(); }
      break;

    case LIBRARY: {
      int rows = books.size() + 1;                          // last row = vibration setting
      int step = (next == BTN_HOLD || prev == BTN_HOLD) ? 5 : 1;
      if (next == BTN_CLICK || next == BTN_HOLD) { sel = min(sel + step, rows - 1); haptic::play(TICK); renderLibrary(); }
      if (prev == BTN_CLICK || prev == BTN_HOLD) { sel = max(sel - step, 0);        haptic::play(TICK); renderLibrary(); }
      if (menu == BTN_CLICK) {
        if (sel < (int)books.size()) { haptic::play(TICK); openBook(books[sel]); }
        else {                                              // toggle vibration
          haptic::enabled() = !haptic::enabled();
          prefs.putBool("haptics", haptic::enabled());
          haptic::play(TICK);                               // only felt when switching on
          renderLibrary();
        }
      }
      if (menu == BTN_LONG) { startUpload(); }
      break;
    }

    case UPLOAD:
      server.handleClient();          // serve the web page / receive uploads
      if (menu == BTN_CLICK) { haptic::play(TICK); stopUpload(); }
      break;

    case MESSAGE:                     // an error is on screen; any button goes back
      if (menu == BTN_CLICK || next == BTN_CLICK || prev == BTN_CLICK) {
        mode = LIBRARY; scanBooks(); renderLibrary();
      }
      break;
  }

  // The battery is measured on every screen update (footer). Nearly flat -> stop.
  if (lastBattery >= 0 && lastBattery <= LOW_BATTERY) lowBatterySleep();
  if (millis() - lastActivity > SLEEP_AFTER_MS) goToSleep();
  delay(5);                           // ~200 passes a second is plenty for buttons
}
