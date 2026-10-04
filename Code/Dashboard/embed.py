from pathlib import Path
root=Path(__file__).resolve().parents[1]
header='#pragma once\n'
for var,file in [('DASHBOARD_HTML','dashboard.html'),('DASHBOARD_JS','dashboard.js')]:
 text=(root/'Dashboard'/file).read_text()
 header+=f'const char {var}[] PROGMEM = R"TRAYPAGE({text})TRAYPAGE";\n'
for folder in ['', 'brick','brick_1','brick_2','brick_3','brick_fix_4']:
 (root/folder/'DashboardPage.h').write_text(header)
