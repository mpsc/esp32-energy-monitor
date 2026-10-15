# coding: utf-8
# Gera include/web_page.h a partir de dashboard/index.html
# Uso: python tools/gen_web_page.py
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HTML_PATH = os.path.join(ROOT, 'dashboard', 'index.html')
OUT_PATH  = os.path.join(ROOT, 'include', 'web_page.h')

with io.open(HTML_PATH, 'r', encoding='utf-8') as f:
    html = f.read()

assert ')HTMLPAGE"' not in html, 'delimitador de raw string presente no HTML!'

header = (
    '#ifndef WEB_PAGE_H\n'
    '#define WEB_PAGE_H\n\n'
    '#include <pgmspace.h>\n\n'
    '// Dashboard HTML (gerado por tools/gen_web_page.py - nao editar manualmente)\n'
    '// Fonte editavel: dashboard/index.html\n'
    'const char PAGE_HTML[] PROGMEM = R"HTMLPAGE(\n'
    + html +
    '\n)HTMLPAGE";\n\n'
    '#endif\n'
)

with io.open(OUT_PATH, 'w', encoding='utf-8', newline='\n') as f:
    f.write(header)

print('OK: web_page.h gerado (%d bytes de HTML)' % len(html))
