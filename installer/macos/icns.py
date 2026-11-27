# icns.py - the application's icon for macOS (.icns) from its Windows
# icon (.ico, the largest image in it), with Pillow:
#   python3 icns.py twist.ico twist.icns
import sys
from PIL import Image
ico = Image.open(sys.argv[1])
big = max(ico.info.get("sizes", [ico.size]))
ico.size = big
img = ico.convert("RGBA")
img.resize((1024, 1024), Image.LANCZOS).save(sys.argv[2], format="ICNS")
print(sys.argv[2], "from", big)
