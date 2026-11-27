# topdf.py - the guide in PDF, with its table of contents filled in
# (LibreOffice, through its Python interface: the .docx keeps a table
# of contents that Word fills when the file is opened):
#   python3 topdf.py TWIST-Console-Guide-utilisateur.docx
import os, subprocess, sys, time
import uno
from com.sun.star.beans import PropertyValue

def prop(name, value):
    p = PropertyValue(); p.Name = name; p.Value = value
    return p

src = os.path.abspath(sys.argv[1])
pdf = os.path.splitext(src)[0] + ".pdf"
office = subprocess.Popen(["soffice", "--headless", "--invisible", "--norestore",
                           "--accept=socket,host=127.0.0.1,port=2083;urp;"])
try:
    resolver = uno.getComponentContext().ServiceManager.createInstanceWithContext(
        "com.sun.star.bridge.UnoUrlResolver", uno.getComponentContext())
    for _ in range(60):
        try:
            ctx = resolver.resolve("uno:socket,host=127.0.0.1,port=2083;urp;StarOffice.ComponentContext")
            break
        except Exception:
            time.sleep(0.5)
    desktop = ctx.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
    doc = desktop.loadComponentFromURL(uno.systemPathToFileUrl(src), "_blank", 0, (prop("Hidden", True),))
    idx = doc.getDocumentIndexes()
    for i in range(idx.getCount()):
        idx.getByIndex(i).update()
    doc.storeToURL(uno.systemPathToFileUrl(pdf), (prop("FilterName", "writer_pdf_Export"),))
    doc.close(True)
    print(pdf)
finally:
    try: desktop.terminate()
    except Exception: pass
    office.wait(timeout=30)
