import pefile
import sys

fn = sys.argv[-4]
out = sys.argv[-3]
old = sys.argv[-2]
new = sys.argv[-1]

f = open(fn, "rb").read()
with open(out, "wb") as g:
    g.write(f.replace(old.encode(), new.encode().ljust(len(old.encode()), b'\x00')))