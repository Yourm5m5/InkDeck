// =============================================================================
//  epub.h – turn an .epub file into plain text the reader can page through
// =============================================================================
//  An EPUB is just a ZIP file with web pages inside:
//
//    book.epub (ZIP)
//    ├── mimetype                     "application/epub+zip"
//    ├── META-INF/container.xml       -> says where the .opf file is
//    └── OEBPS/
//        ├── content.opf              -> title, list of files (manifest),
//        │                               reading order (spine)
//        ├── chapter1.xhtml           -> the actual text, as HTML
//        ├── chapter2.xhtml
//        └── images, CSS, fonts ...   (ignored – e-paper shows text only)
//
//  convert() does this once per book:
//    1. read the ZIP's table of contents (the "central directory")
//    2. container.xml -> path of content.opf
//    3. content.opf   -> title + chapter files in reading order
//    4. for each chapter: unzip it, strip the HTML tags, write plain text
//
//  The output text uses three special characters that layoutPage() understands:
//    '\n'  end of a line / paragraph
//    '\t'  start of a paragraph -> first line is indented
//    '\f'  start of a chapter   -> always begins on a new page
//
//  This file has no Arduino code in it, so it can also be compiled and tested
//  on a normal computer (see pc_tests/test_epub.cpp).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace epub {

// Where convert() reads the .epub from. On the reader this wraps an SD-card File.
class Source {
 public:
  virtual ~Source() {}
  virtual uint32_t size() = 0;
  virtual bool readAt(uint32_t offset, uint8_t* dst, uint32_t len) = 0;
};

// Where the plain text goes. On the reader this wraps an SD-card File.
class Sink {
 public:
  virtual ~Sink() {}
  virtual bool write(const char* data, size_t len) = 0;
};

// Things that depend on the platform, passed in by the caller.
struct Options {
  // Unpack raw DEFLATE data (the compression ZIP uses). Must fill exactly outLen bytes.
  bool (*inflate)(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) = nullptr;
  // Does the screen font have this character? nullptr = assume it has everything.
  bool (*hasGlyph)(uint32_t codepoint) = nullptr;
  // Called after each chapter so the screen can show progress (optional).
  void (*progress)(int done, int total) = nullptr;
  // Refuse chapters bigger than this when unpacked (protects RAM).
  uint32_t maxEntryBytes = 4UL * 1024 * 1024;
};

struct Result {
  bool ok = false;
  std::string error;               // why it failed, shown on screen
  std::string title;               // from <dc:title>, empty if missing
  std::vector<uint32_t> chapters;  // byte offset in the text where each chapter starts
  uint32_t textBytes = 0;          // total size of the text written
};

Result convert(Source& src, Sink& out, const Options& opt);

// ---- pieces exposed for testing ---------------------------------------------
// HTML/XHTML -> reader text ('\t' paragraphs, blank line around headings).
std::string htmlToText(const char* html, size_t len, bool (*hasGlyph)(uint32_t));
// Resolve a link inside the book: ("OEBPS/Text/", "../Images/a%20b.png#x") -> "OEBPS/Images/a b.png"
std::string resolvePath(const std::string& baseDir, const std::string& href);

}  // namespace epub
