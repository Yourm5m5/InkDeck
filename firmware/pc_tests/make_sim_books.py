"""Bigger books for the simulation (run: python3 make_sim_books.py <sdcard folder>)."""
import sys, os, random
from ebooklib import epub
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
random.seed(1)
words = ("the a lantern quiet harbour drifted across morning while she remembered every letter "
         "her grandmother had written about storms ships and the long road north").split()
def para(n): return " ".join(random.choice(words) for _ in range(n)).capitalize() + "."
b = epub.EpubBook(); b.set_identifier("sim"); b.set_title("The Lighthouse Keeper’s Daughter"); b.set_language("en")
chs = []
for i in range(1, 6):
    c = epub.EpubHtml(title=f"Chapter {i}", file_name=f"text/ch{i}.xhtml")
    body = f"<h1>Chapter {i}</h1>" + "".join(f"<p>“{para(random.randint(15, 60))}” she said — {para(10)}</p>" for _ in range(random.randint(8, 25)))
    c.content = body; b.add_item(c); chs.append(c)
b.toc = chs; b.add_item(epub.EpubNcx()); b.add_item(epub.EpubNav()); b.spine = ["nav"] + chs
epub.write_epub(os.path.join(out, "Lighthouse.epub"), b)
open(os.path.join(out, "Notes.txt"), "w").write("\n".join(para(40) for _ in range(60)))
open(os.path.join(out, "broken.epub"), "w").write("not really an epub" * 10)
print("books written to", out)
