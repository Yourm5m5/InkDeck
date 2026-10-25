"""Builds sample EPUBs for test_epub.cpp (run: python3 make_test_books.py)."""
import zipfile, subprocess, os
from ebooklib import epub

# 1. A normal EPUB 3 made by a real library (cover, nav, CSS, nested folders)
b = epub.EpubBook()
b.set_identifier("inkdeck-test-1"); b.set_title("The Test & Trial"); b.set_language("en")
b.add_author("A. Writer")
css = epub.EpubItem(uid="css", file_name="Styles/s.css", media_type="text/css", content=b"p{margin:0}")
b.add_item(css)
c1 = epub.EpubHtml(title="One", file_name="Text/chapter 1.xhtml")
c1.content = ("<h1>Chapter One</h1><p>It was a  dark\n and stormy night&mdash;the rain fell in "
              "&ldquo;torrents&rdquo;. Café &amp; bar&#8217;s door&#x2026;</p>"
              "<p>Second para<br/>with a break.</p><script>var x='<p>NO</p>';</script>"
              "<!-- hidden <p>comment</p> --><ul><li>apple</li><li>pear</li></ul><hr/>"
              "<p>A&nbsp;B soft­hyphen zero​width</p>")
c2 = epub.EpubHtml(title="Two", file_name="Text/ch2.xhtml")
c2.content = "<h2>Chapter Two</h2><div><p>Short.</p></div><table><tr><td>a</td><td>b</td></tr></table>"
img = epub.EpubHtml(title="Pic", file_name="Text/pic.xhtml")
img.content = "<div><img src='../Images/x.png' alt='pic'/></div>"
for c in (c1, img, c2): b.add_item(c)
b.toc = (c1, c2); b.add_item(epub.EpubNcx()); b.add_item(epub.EpubNav())
b.spine = ["nav", c1, img, c2]
epub.write_epub("normal.epub", b)

# 2. Hand-made EPUB 2: OPF in the root, single quotes, attributes in odd order,
#    STORED + DEFLATE mixed, linear="no" item, unknown entity, ../ path
with zipfile.ZipFile("handmade.epub", "w") as z:
    z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
    z.writestr("META-INF/container.xml",
        "<?xml version='1.0'?><container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>"
        "<rootfiles><rootfile media-type='application/oebps-package+xml' full-path='book.opf'/></rootfiles></container>",
        compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("book.opf",
        "<?xml version='1.0' encoding='utf-8'?><package xmlns='http://www.idpf.org/2007/opf' version='2.0'>"
        "<metadata xmlns:dc='http://purl.org/dc/elements/1.1/'><dc:title>Hand &amp; Made</dc:title></metadata>"
        "<manifest><item media-type='application/xhtml+xml' href='a/../x/one.html' id='one'/>"
        "<item href='x/two.html' media-type='application/xhtml+xml' id='two'/>"
        "<item href='x/notes.html' media-type='application/xhtml+xml' id='notes'/></manifest>"
        "<spine><itemref idref='notes' linear='no'/><itemref idref='one'/><itemref idref='two'/></spine></package>",
        compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("x/one.html", "<html><head><title>T</title><style>p{}</style></head><body><p>Hello &bogus; world</p></body></html>",
               compress_type=zipfile.ZIP_STORED)
    z.writestr("x/two.html", "<html><body><p>" + "word " * 5000 + "</p></body></html>", compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("x/notes.html", "<p>NOTES SHOULD BE SKIPPED</p>", compress_type=zipfile.ZIP_DEFLATED)

# 3. Made by the 'zip' command reading from a pipe -> uses "data descriptors"
#    (sizes are 0 in the local header, real sizes only in the central directory)
os.makedirs("dd/META-INF", exist_ok=True)
open("dd/META-INF/container.xml", "w").write("<container><rootfiles><rootfile full-path=\"c.opf\"/></rootfiles></container>")
open("dd/c.opf", "w").write("<package><metadata><title>Piped</title></metadata><manifest><item id=\"a\" href=\"a.xhtml\" media-type=\"application/xhtml+xml\"/></manifest><spine><itemref idref=\"a\"/></spine></package>")
open("dd/a.xhtml", "w").write("<p>Piped text " + "x" * 3000 + "</p>")
if os.path.exists("piped.epub"): os.remove("piped.epub")
for f in ["META-INF/container.xml", "c.opf", "a.xhtml"]:
    subprocess.run(f"cat {f} | zip -q -fz- ../piped.epub - && printf '@ -\\n@={f}\\n' | zipnote -w ../piped.epub", shell=True, cwd="dd", check=True)

# 4. DRM'd book (encrypted chapter listed in encryption.xml)
with zipfile.ZipFile("drm.epub", "w") as z:
    z.writestr("META-INF/encryption.xml", "<encryption><enc:EncryptedData><enc:CipherData><enc:CipherReference URI=\"OEBPS/ch1.xhtml\"/></enc:CipherData></enc:EncryptedData></encryption>")
    z.writestr("META-INF/container.xml", "<rootfile full-path=\"OEBPS/c.opf\"/>")

# 5. Not a zip at all
open("notazip.epub", "w").write("hello")
print("ok")
