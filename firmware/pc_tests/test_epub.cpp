// Host test for src/epub.cpp – runs on a normal computer, not the reader.
//   g++ -std=c++17 -I../src test_epub.cpp ../src/epub.cpp -lz -o test_epub && ./test_epub
// zlib stands in for the ESP32's built-in ROM inflate.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <zlib.h>
#include "epub.h"

struct FileSource : epub::Source {
  FILE* f;
  explicit FileSource(const char* p) { f = fopen(p, "rb"); }
  ~FileSource() { if (f) fclose(f); }
  uint32_t size() override { fseek(f, 0, SEEK_END); return (uint32_t)ftell(f); }
  bool readAt(uint32_t off, uint8_t* d, uint32_t n) override {
    return fseek(f, off, SEEK_SET) == 0 && fread(d, 1, n, f) == n;
  }
};
struct StringSink : epub::Sink {
  std::string s;
  bool write(const char* d, size_t n) override { s.append(d, n); return true; }
};

static bool zInflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
  z_stream z{};
  if (inflateInit2(&z, -15) != Z_OK) return false;          // -15 = raw DEFLATE, like ZIP
  z.next_in = (Bytef*)in; z.avail_in = inLen; z.next_out = out; z.avail_out = outLen;
  int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  return r == Z_STREAM_END && z.total_out == outLen;
}
// Pretend the font only has ASCII + Latin-1, like a small e-reader font.
static bool asciiFont(uint32_t cp) { return cp < 0x7F || (cp >= 0xA0 && cp <= 0xFF); }

static std::string show(const std::string& s) {
  std::string o;
  for (char c : s) o += c == '\t' ? std::string("<TAB>") : c == '\f' ? std::string("<PAGE>\n") : std::string(1, c);
  return o;
}

static epub::Result run(const char* path, StringSink& out, bool (*glyph)(uint32_t) = asciiFont) {
  FileSource src(path);
  epub::Options o; o.inflate = zInflate; o.hasGlyph = glyph;
  return epub::convert(src, out, o);
}

int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
  // ---- path resolving
  CHECK(epub::resolvePath("OEBPS/Text/", "../Images/a%20b.png#x") == "OEBPS/Images/a b.png");
  CHECK(epub::resolvePath("", "./ch1.xhtml") == "ch1.xhtml");
  CHECK(epub::resolvePath("OEBPS/", "/root.html") == "root.html");

  // ---- html -> text basics
  std::string t = epub::htmlToText("<p>a</p><p>b</p>", 16, nullptr);
  CHECK(t == "\ta\n\tb");
  t = epub::htmlToText("<h1>T</h1><p>x  y\n z</p>", 24, nullptr);
  CHECK(t == "T\n\n\tx y z");
  const char* q = "<p>\xE2\x80\x9CHi\xE2\x80\x9D \xE2\x80\x94 ok&#8230;</p>";
  CHECK(epub::htmlToText(q, strlen(q), asciiFont) == "\t\"Hi\" -- ok...");
  CHECK(epub::htmlToText(q, strlen(q), nullptr) == "\t\xE2\x80\x9CHi\xE2\x80\x9D \xE2\x80\x94 ok\xE2\x80\xA6");
  const char* acc = "<p>Caf\xC3\xA9</p>";
  CHECK(epub::htmlToText(acc, strlen(acc), [](uint32_t c) { return c < 0x7F; }) == "\tCafe");

  // ---- real books
  const char* books[] = {"normal.epub", "handmade.epub", "piped.epub"};
  for (const char* b : books) {
    StringSink out;
    epub::Result r = run(b, out);
    printf("== %s: ok=%d title='%s' chapters=%zu bytes=%u %s\n", b, r.ok, r.title.c_str(),
           r.chapters.size(), r.textBytes, r.error.c_str());
    CHECK(r.ok);
    CHECK(out.s.size() == r.textBytes);
    for (size_t i = 1; i < r.chapters.size(); i++) CHECK(out.s[r.chapters[i] - 1] == '\f');
    std::string preview = out.s.substr(0, 700);
    printf("%s\n...\n\n", show(preview).c_str());

    if (!strcmp(b, "normal.epub")) {
      CHECK(r.title == "The Test & Trial");
      CHECK(out.s.find("NO") == std::string::npos);          // <script> skipped
      CHECK(out.s.find("comment") == std::string::npos);     // <!-- --> skipped
      CHECK(out.s.find("night--the rain") != std::string::npos);
      CHECK(out.s.find("Caf\xC3\xA9 & bar's door...") != std::string::npos);
      CHECK(out.s.find("softhyphen zerowidth") != std::string::npos);
      CHECK(out.s.find("A B") != std::string::npos);
      CHECK(out.s.find("Chapter Two") != std::string::npos);
    }
    if (!strcmp(b, "handmade.epub")) {
      CHECK(r.title == "Hand & Made");
      CHECK(r.chapters.size() == 2);
      CHECK(out.s.find("NOTES") == std::string::npos);       // linear="no" skipped
      CHECK(out.s.find("&bogus;") != std::string::npos);     // unknown entity left alone
      CHECK(out.s.find("\tHello") == 0);                     // <head><title> skipped
    }
  }

  // ---- books that should fail politely
  const char* bad[] = {"drm.epub", "notazip.epub"};
  for (const char* b : bad) {
    StringSink out;
    epub::Result r = run(b, out);
    printf("== %s: ok=%d error='%s'\n", b, r.ok, r.error.c_str());
    CHECK(!r.ok && !r.error.empty());
  }

  printf("\n%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
  return fails ? 1 : 0;
}
