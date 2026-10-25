// =============================================================================
//  epub.cpp – EPUB (ZIP + XHTML) -> plain text.   See epub.h for the overview.
// =============================================================================
//  Sections
//    1. Small helpers      – little-endian reads, text helpers
//    2. ZIP reading        – central directory, unpacking one file
//    3. Paths              – resolving links between files in the book
//    4. Characters         – UTF-8, HTML entities, swaps for missing glyphs
//    5. HTML -> text       – the tag-stripping state machine
//    6. OPF parsing        – title, manifest, spine
//    7. convert()          – puts it all together
#include "epub.h"

#include <stdlib.h>
#include <string.h>

namespace epub {

// ---------------------------------------------------------------------------
// 1. SMALL HELPERS
// ---------------------------------------------------------------------------
// ZIP stores numbers little-endian (lowest byte first).
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (lower(a[i]) != lower(b[i])) return false;
  return true;
}

static bool endsWithI(const std::string& s, const char* suffix) {
  size_t n = strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; i++)
    if (lower(s[s.size() - n + i]) != lower(suffix[i])) return false;
  return true;
}

static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Remove spaces / newlines / tabs / page breaks from both ends.
static void trim(std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (isSpace(s[a]) || s[a] == '\f')) a++;
  while (b > a && (isSpace(s[b - 1]) || s[b - 1] == '\f')) b--;
  // keep a leading '\t' (paragraph indent) if the text starts with one
  if (a > 0 && s[a - 1] == '\t') a--;
  s = s.substr(a, b - a);
}

// Value of an attribute inside one tag:  getAttr("<item id=\"c1\" href=\"a.xhtml\"/>", "href") -> "a.xhtml"
// The name must have a space before it, so "id" doesn't match inside "idref".
static std::string getAttr(const std::string& tag, const char* name) {
  size_t n = strlen(name);
  for (size_t p = tag.find(name); p != std::string::npos; p = tag.find(name, p + 1)) {
    if (p == 0 || !isSpace(tag[p - 1])) continue;
    size_t q = p + n;
    while (q < tag.size() && isSpace(tag[q])) q++;
    if (q >= tag.size() || tag[q] != '=') continue;
    q++;
    while (q < tag.size() && isSpace(tag[q])) q++;
    if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) continue;
    char quote = tag[q];
    size_t end = tag.find(quote, q + 1);
    if (end == std::string::npos) return "";
    return tag.substr(q + 1, end - q - 1);
  }
  return "";
}

// ---------------------------------------------------------------------------
// 2. ZIP READING
// ---------------------------------------------------------------------------
// A ZIP file ends with a "central directory": a list of every file inside,
// with its name, size, compression method and where its data starts. We read
// that list once, then jump straight to the files we need.
//
//   [local header + data] [local header + data] ... [central directory] [end record]
struct Entry {
  std::string name;
  uint16_t method;     // 0 = stored (not compressed), 8 = DEFLATE
  uint32_t csize;      // compressed size
  uint32_t usize;      // size once unpacked
  uint32_t localOff;   // where this file's local header starts
};

static bool readDirectory(Source& s, std::vector<Entry>& out, std::string& err) {
  uint32_t sz = s.size();
  if (sz < 22) { err = "Not a valid EPUB (file too small)"; return false; }

  // The "end of central directory" record is in the last 22 bytes, unless the
  // ZIP has a comment (up to 64 KB) after it. Search backwards for its signature.
  uint32_t tail = sz < 65557 ? sz : 65557;
  std::vector<uint8_t> buf(tail);
  if (!s.readAt(sz - tail, buf.data(), tail)) { err = "Couldn't read the file"; return false; }
  int eocd = -1;
  for (int i = (int)tail - 22; i >= 0; i--)
    if (rd32(&buf[i]) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { err = "Not a valid EPUB (no ZIP directory)"; return false; }

  uint16_t count = rd16(&buf[eocd + 10]);
  uint32_t cdSize = rd32(&buf[eocd + 12]);
  uint32_t cdOff  = rd32(&buf[eocd + 16]);
  if (cdOff == 0xFFFFFFFF || (uint64_t)cdOff + cdSize > sz) { err = "EPUB too big or damaged"; return false; }

  std::vector<uint8_t> cd(cdSize);
  if (!s.readAt(cdOff, cd.data(), cdSize)) { err = "Couldn't read the file"; return false; }

  // Each central-directory record: fixed 46 bytes, then name, extra, comment.
  uint32_t p = 0;
  for (uint16_t i = 0; i < count; i++) {
    if (p + 46 > cdSize || rd32(&cd[p]) != 0x02014b50) { err = "EPUB directory is damaged"; return false; }
    Entry e;
    e.method   = rd16(&cd[p + 10]);
    e.csize    = rd32(&cd[p + 20]);
    e.usize    = rd32(&cd[p + 24]);
    uint16_t n = rd16(&cd[p + 28]), x = rd16(&cd[p + 30]), c = rd16(&cd[p + 32]);
    e.localOff = rd32(&cd[p + 42]);
    if (p + 46 + n > cdSize) { err = "EPUB directory is damaged"; return false; }
    e.name.assign((const char*)&cd[p + 46], n);
    p += 46 + n + x + c;
    out.push_back(e);
  }
  return true;
}

static const Entry* findEntry(const std::vector<Entry>& dir, const std::string& name) {
  for (auto& e : dir) if (e.name == name) return &e;
  for (auto& e : dir) if (iequals(e.name, name)) return &e;   // some books get the case wrong
  return nullptr;
}

// Unpack one file into a malloc'd, 0-terminated buffer (caller frees).
// Large buffers end up in PSRAM automatically on the ESP32-S3.
static char* readEntry(Source& s, const Entry& e, const Options& o, std::string& err) {
  if (e.usize > o.maxEntryBytes) { err = "Chapter too big: " + e.name; return nullptr; }

  // The local header repeats the name + extra field, and their lengths can
  // differ from the central directory, so read it to find where data starts.
  uint8_t lh[30];
  if (!s.readAt(e.localOff, lh, 30) || rd32(lh) != 0x04034b50) { err = "Damaged entry: " + e.name; return nullptr; }
  uint32_t dataOff = e.localOff + 30 + rd16(lh + 26) + rd16(lh + 28);

  char* out = (char*)malloc(e.usize + 1);
  if (!out) { err = "Out of memory"; return nullptr; }
  bool ok = false;
  if (e.method == 0) {                                   // stored: just copy
    ok = e.csize == e.usize && s.readAt(dataOff, (uint8_t*)out, e.usize);
  } else if (e.method == 8) {                            // DEFLATE: read, then unpack
    uint8_t* in = (uint8_t*)malloc(e.csize ? e.csize : 1);
    if (in && s.readAt(dataOff, in, e.csize))
      ok = o.inflate(in, e.csize, (uint8_t*)out, e.usize);
    free(in);
  } else {
    err = "Unsupported compression in " + e.name;
  }
  if (!ok) {
    if (err.empty()) err = "Couldn't unpack " + e.name;
    free(out);
    return nullptr;
  }
  out[e.usize] = 0;
  return out;
}

static bool readEntryString(Source& s, const std::vector<Entry>& dir, const std::string& name,
                            const Options& o, std::string& text, std::string& err) {
  const Entry* e = findEntry(dir, name);
  if (!e) { err = "Missing " + name; return false; }
  char* p = readEntry(s, *e, o, err);
  if (!p) return false;
  text.assign(p, e->usize);
  free(p);
  return true;
}

// ---------------------------------------------------------------------------
// 3. PATHS
// ---------------------------------------------------------------------------
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = lower(c);
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "My%20Book.xhtml" -> "My Book.xhtml"
static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && hexVal(s[i + 1]) >= 0 && hexVal(s[i + 2]) >= 0) {
      o += (char)(hexVal(s[i + 1]) * 16 + hexVal(s[i + 2]));
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

static std::string dirOf(const std::string& path) {
  size_t s = path.rfind('/');
  return s == std::string::npos ? "" : path.substr(0, s + 1);
}

// Links in the OPF are relative to the OPF's folder and may use "..", "%20" and "#anchors".
std::string resolvePath(const std::string& baseDir, const std::string& href) {
  std::string h = href.substr(0, href.find('#'));
  h = urlDecode(h);
  std::string full = (!h.empty() && h[0] == '/') ? h.substr(1) : baseDir + h;

  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= full.size()) {
    size_t slash = full.find('/', start);
    if (slash == std::string::npos) slash = full.size();
    std::string seg = full.substr(start, slash - start);
    if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (!seg.empty() && seg != ".") parts.push_back(seg);
    start = slash + 1;
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); i++) { if (i) out += '/'; out += parts[i]; }
  return out;
}

// ---------------------------------------------------------------------------
// 4. CHARACTERS
// ---------------------------------------------------------------------------
static void appendUtf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) { o += (char)cp; }
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) {
    o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
  } else {
    o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
    o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
  }
}

// Read one UTF-8 character starting at s[i]; moves i past it. Bad bytes -> 0xFFFD.
static uint32_t nextUtf8(const char* s, size_t len, size_t& i) {
  uint8_t c = (uint8_t)s[i++];
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
  if (extra < 0) return 0xFFFD;
  uint32_t cp = c & (0x3F >> extra);
  for (int k = 0; k < extra; k++) {
    if (i >= len || ((uint8_t)s[i] & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | ((uint8_t)s[i++] & 0x3F);
  }
  return cp;
}

// Named HTML entities that actually show up in books.
struct NamedEntity { const char* name; uint32_t cp; };
static const NamedEntity ENTITIES[] = {
  {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
  {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018},
  {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"laquo", 0xAB}, {"raquo", 0xBB},
  {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"deg", 0xB0}, {"middot", 0xB7},
  {"bull", 0x2022}, {"shy", 0xAD}, {"eacute", 0xE9}, {"egrave", 0xE8}, {"agrave", 0xE0},
  {"aacute", 0xE1}, {"ccedil", 0xE7}, {"ouml", 0xF6}, {"uuml", 0xFC}, {"auml", 0xE4},
  {"times", 0xD7}, {"pound", 0xA3}, {"euro", 0x20AC}, {"sect", 0xA7}, {"para", 0xB6},
};

// If the font is missing a character, swap it for something it does have.
// Curly quotes -> straight quotes, em dash -> "--", accented letters -> plain letters.
static const char* fallbackFor(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: case 0xAB: case 0xBB: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212: return "-";
    case 0x2014: case 0x2015: return "--";
    case 0x2026: return "...";
    case 0x2022: case 0xB7: return "*";
    case 0x2122: return "(TM)";
    case 0xA9: return "(c)";
    case 0x20AC: return "EUR";
  }
  // Latin-1 accented letters (U+00C0..U+00FF) -> nearest plain letter
  static const char* LATIN1 = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  if (cp >= 0xC0 && cp <= 0xFF) {
    static char one[2] = {0, 0};
    one[0] = LATIN1[cp - 0xC0];
    return one;
  }
  return "?";
}

// ---------------------------------------------------------------------------
// 5. HTML -> TEXT
// ---------------------------------------------------------------------------
// Walks through the HTML once. Tags are not drawn; instead they decide where
// line breaks, paragraph indents and blank lines go. Text between tags is
// copied, with runs of spaces/newlines squashed into one space (like a browser).
//
// "Pending" flags: when we see </p> we don't write '\n' straight away; we
// remember that a break is needed and write it just before the next real
// character. That stops empty <div>s and spacing from piling up blank lines.
class TextBuilder {
 public:
  explicit TextBuilder(bool (*hasGlyph)(uint32_t)) : hasGlyph_(hasGlyph) {}

  void space()            { space_ = true; }
  void lineBreak(int n)   { if (n > nl_) nl_ = n; space_ = false; para_ = false; }   // n=1 new line, n=2 blank line
  void paragraph()        { lineBreak(1); para_ = true; }
  void addBreakFromBr()   { nl_ = nl_ < 2 ? nl_ + 1 : 2; space_ = false; para_ = false; }

  void put(uint32_t cp) {
    // Spaces of every kind collapse into one pending space.
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0 ||
        (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x3000) { space_ = true; return; }
    // Invisible characters: soft hyphen, zero-width space/joiners, byte-order mark
    if (cp == 0xAD || (cp >= 0x200B && cp <= 0x200D) || cp == 0xFEFF || cp < 0x20) return;

    flushPending();
    if (hasGlyph_ && !hasGlyph_(cp)) { out_ += fallbackFor(cp); return; }
    appendUtf8(out_, cp);
  }
  void putAscii(const char* s) { while (*s) put((uint8_t)*s++); }

  std::string take() { std::string s; s.swap(out_); trim(s); return s; }

 private:
  void flushPending() {
    if (!out_.empty()) {
      for (int k = 0; k < nl_; k++) out_ += '\n';
      if (nl_ == 0 && space_) out_ += ' ';
    }
    if (para_) out_ += '\t';
    nl_ = 0; space_ = false; para_ = false;
  }

  bool (*hasGlyph_)(uint32_t);
  std::string out_;
  int  nl_ = 0;
  bool space_ = false, para_ = false;
};

static bool inList(const std::string& n, const char* const* list) {
  for (; *list; list++) if (n == *list) return true;
  return false;
}

static const char* const BLOCK_TAGS[] = {
  "div", "section", "article", "blockquote", "tr", "table", "ul", "ol", "dl", "dt", "dd",
  "figure", "figcaption", "pre", "header", "footer", "aside", "nav", "body", "address", nullptr };
static const char* const SKIP_TAGS[] = { "head", "script", "style", "svg", "math", nullptr };

std::string htmlToText(const char* h, size_t len, bool (*hasGlyph)(uint32_t)) {
  TextBuilder tb(hasGlyph);
  std::string skipUntil;            // inside <head>/<script>/...: ignore until this closing tag
  size_t i = 0;

  while (i < len) {
    char c = h[i];

    // ---- tags --------------------------------------------------------------
    if (c == '<') {
      if (strncmp(h + i, "<!--", 4) == 0) {                     // comment
        const char* e = strstr(h + i + 4, "-->");
        i = e ? (size_t)(e - h) + 3 : len;
        continue;
      }
      // find the closing '>' (ignoring any '>' inside quotes)
      size_t j = i + 1;
      char q = 0;
      while (j < len && (q || h[j] != '>')) {
        if (q) { if (h[j] == q) q = 0; }
        else if (h[j] == '"' || h[j] == '\'') q = h[j];
        j++;
      }
      if (h[i + 1] == '!' || h[i + 1] == '?') { i = j + 1; continue; }   // <!DOCTYPE>, <?xml?>

      bool closing = (h[i + 1] == '/');
      size_t k = i + 1 + (closing ? 1 : 0);
      std::string name;
      while (k < j && !isSpace(h[k]) && h[k] != '/' && h[k] != '>') name += lower(h[k++]);
      size_t colon = name.rfind(':');                             // <html:p> -> p
      if (colon != std::string::npos) name = name.substr(colon + 1);
      bool selfClosing = (j > 0 && h[j - 1] == '/');
      i = j + 1;

      if (!skipUntil.empty()) {                                   // waiting for </head> etc.
        if (closing && name == skipUntil) skipUntil.clear();
        continue;
      }
      if (!closing && !selfClosing && inList(name, SKIP_TAGS)) { skipUntil = name; continue; }

      if (name == "p") {
        if (closing) tb.lineBreak(1); else tb.paragraph();
      } else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
        tb.lineBreak(2);                                          // blank line around headings
      } else if (name == "br") {
        tb.addBreakFromBr();
      } else if (name == "hr") {
        tb.lineBreak(2); tb.putAscii("* * *"); tb.lineBreak(2);
      } else if (name == "li") {
        tb.lineBreak(1);
        if (!closing) tb.putAscii("- ");
      } else if (name == "td" || name == "th") {
        tb.space();
      } else if (inList(name, BLOCK_TAGS)) {
        tb.lineBreak(1);
      }
      // anything else (span, em, a, img ...) adds nothing
      continue;
    }

    if (!skipUntil.empty()) { i++; continue; }

    // ---- entities: &amp;  &#8217;  &#x2019; --------------------------------
    if (c == '&') {
      size_t semi = i + 1;
      while (semi < len && semi < i + 12 && h[semi] != ';' && h[semi] != '&' && !isSpace(h[semi])) semi++;
      if (semi < len && h[semi] == ';') {
        std::string ent(h + i + 1, semi - i - 1);
        uint32_t cp = 0;
        if (ent.size() > 1 && ent[0] == '#') {
          cp = (ent[1] == 'x' || ent[1] == 'X') ? (uint32_t)strtoul(ent.c_str() + 2, nullptr, 16)
                                                : (uint32_t)strtoul(ent.c_str() + 1, nullptr, 10);
        } else {
          for (auto& e : ENTITIES) if (ent == e.name) { cp = e.cp; break; }
        }
        if (cp) { tb.put(cp); i = semi + 1; continue; }
      }
      tb.put('&');                                              // not an entity: keep the '&'
      i++;
      continue;
    }

    // ---- normal text ---------------------------------------------------------
    tb.put(nextUtf8(h, len, i));
  }
  return tb.take();
}

// ---------------------------------------------------------------------------
// 6. OPF PARSING
// ---------------------------------------------------------------------------
// Calls fn(localName, wholeTag, textAfterTag) for every opening tag in an XML file.
template <typename F>
static void forEachTag(const std::string& xml, F fn) {
  size_t i = 0;
  while ((i = xml.find('<', i)) != std::string::npos) {
    size_t j = xml.find('>', i);
    if (j == std::string::npos) break;
    if (xml[i + 1] != '/' && xml[i + 1] != '!' && xml[i + 1] != '?') {
      size_t k = i + 1;
      std::string name;
      while (k < j && !isSpace(xml[k]) && xml[k] != '/') name += lower(xml[k++]);
      size_t colon = name.rfind(':');
      if (colon != std::string::npos) name = name.substr(colon + 1);
      size_t textEnd = xml.find('<', j);
      std::string text = xml.substr(j + 1, (textEnd == std::string::npos ? xml.size() : textEnd) - j - 1);
      fn(name, xml.substr(i, j - i + 1), text);
    }
    i = j + 1;
  }
}

struct ManifestItem { std::string id, href, type, props; };

// ---------------------------------------------------------------------------
// 7. CONVERT
// ---------------------------------------------------------------------------
Result convert(Source& src, Sink& out, const Options& opt) {
  Result r;
  if (!opt.inflate) { r.error = "No inflate function"; return r; }

  // 1. ZIP directory
  std::vector<Entry> dir;
  if (!readDirectory(src, dir, r.error)) return r;

  // DRM check: shop-bought books list their encrypted chapters in encryption.xml.
  // (Only fonts being "obfuscated" is fine and common, so look at file types.)
  std::string enc, ignore;
  if (findEntry(dir, "META-INF/encryption.xml") &&
      readEntryString(src, dir, "META-INF/encryption.xml", opt, enc, ignore)) {
    bool drm = false;
    forEachTag(enc, [&](const std::string& n, const std::string& tag, const std::string&) {
      if (n == "cipherreference") {
        std::string uri = getAttr(tag, "URI");
        if (endsWithI(uri, "html") || endsWithI(uri, "htm") || endsWithI(uri, "xml")) drm = true;
      }
    });
    if (drm) { r.error = "This book has DRM (copy protection)"; return r; }
  }

  // 2. container.xml -> where the OPF is
  std::string container;
  if (!readEntryString(src, dir, "META-INF/container.xml", opt, container, r.error)) return r;
  std::string opfPath;
  forEachTag(container, [&](const std::string& n, const std::string& tag, const std::string&) {
    if (n == "rootfile" && opfPath.empty()) opfPath = getAttr(tag, "full-path");
  });
  if (opfPath.empty()) { r.error = "container.xml has no rootfile"; return r; }

  // 3. OPF -> title, manifest (id -> file), spine (reading order)
  std::string opf;
  if (!readEntryString(src, dir, opfPath, opt, opf, r.error)) return r;
  std::string baseDir = dirOf(opfPath);

  std::vector<ManifestItem> manifest;
  std::vector<std::string> spine;
  forEachTag(opf, [&](const std::string& n, const std::string& tag, const std::string& text) {
    if (n == "title" && r.title.empty()) {
      r.title = htmlToText(text.c_str(), text.size(), opt.hasGlyph);
    } else if (n == "item") {
      manifest.push_back({getAttr(tag, "id"), getAttr(tag, "href"), getAttr(tag, "media-type"),
                          getAttr(tag, "properties")});
    } else if (n == "itemref") {
      if (getAttr(tag, "linear") != "no") spine.push_back(getAttr(tag, "idref"));
    }
  });
  // the title must be one line
  for (auto& ch : r.title) if (ch == '\n' || ch == '\t' || ch == '\f') ch = ' ';
  if (spine.empty()) { r.error = "Book has no reading order (spine)"; return r; }

  // 4. Chapters, in spine order
  int done = 0, failed = 0;
  for (auto& idref : spine) {
    done++;
    const ManifestItem* item = nullptr;
    for (auto& m : manifest) if (m.id == idref) { item = &m; break; }
    if (!item || item->type.find("html") == std::string::npos) continue;   // not a text page
    if (item->props.find("nav") != std::string::npos) continue;            // EPUB 3 contents page: skip,
                                                                           // the reader has chapter jumps

    std::string path = resolvePath(baseDir, item->href);
    const Entry* e = findEntry(dir, path);
    std::string err;
    char* html = e ? readEntry(src, *e, opt, err) : nullptr;
    if (!html) { failed++; continue; }                        // skip a broken chapter, keep going

    std::string text = htmlToText(html, e->usize, opt.hasGlyph);
    free(html);
    if (text.empty()) continue;                               // e.g. a cover page that's only an image

    if (r.textBytes > 0) {                                    // '\f' = new chapter starts on a new page
      if (!out.write("\f", 1)) { r.error = "SD card write failed"; return r; }
      r.textBytes += 1;
    }
    r.chapters.push_back(r.textBytes);
    if (!out.write(text.data(), text.size())) { r.error = "SD card write failed"; return r; }
    r.textBytes += text.size();
    if (opt.progress) opt.progress(done, (int)spine.size());
  }

  if (r.textBytes == 0) {
    r.error = failed ? "Couldn't read any chapters" : "No text found in this book";
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace epub
