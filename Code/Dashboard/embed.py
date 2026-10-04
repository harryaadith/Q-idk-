from pathlib import Path
root=Path(__file__).resolve().parents[1]
header='#pragma once\n'
for var,file in [('DASHBOARD_HTML','dashboard.html'),('DASHBOARD_JS','dashboard.js'),('LAYOUT_JS','layout.js')]:
 text=(root/'Dashboard'/file).read_text()
 header+=f'const char {var}[] PROGMEM = R"TRAYPAGE({text})TRAYPAGE";\n'
(root/'brick'/'DashboardPage.h').write_text(header)
