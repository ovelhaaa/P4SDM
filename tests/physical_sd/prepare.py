from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[2]
target=root/'.pio'/'m19p'
target.mkdir(exist_ok=True)
shutil.copytree(root/'src',target/'src',dirs_exist_ok=True)
shutil.copytree(root/'lib',target/'lib',dirs_exist_ok=True)
for pattern in ('*.ino','*.h'):
    for file in root.glob(pattern):
        shutil.copyfile(file,target/file.name)
ini=(root/'platformio.ini').read_text()
ini=ini.replace('boards_dir = boards','boards_dir = '+(root/'boards').as_posix())
(target/'platformio.ini').write_text(ini)
samples=target/'src/app/samples.cpp'
s=samples.read_text()
s=s.replace('void load(int index) { load_name(names[index]); }','void load(int index) { load_name(names[index]); }\n#include "qstore.inc"')
s=s.replace('    transients::poll();','    qstore_poll();\n    transients::poll();')
s=s.replace('} // namespace samples','void qrequest(int op) { qjob.store(op); }\n} // namespace samples')
samples.write_text(s)
shutil.copyfile(Path(__file__).with_name('qstore.inc'),target/'src/app/qstore.inc')
store=target/'src/app/qstore.inc'
store.write_text(store.read_text().replace('Serial.printf(', 'qprintf('))
header=target/'src/app/samples.h'
header.write_text(header.read_text().replace('namespace samples {','namespace samples {\nvoid qrequest(int op);'))
app=target/'src/app_main.cpp'
s=app.read_text().replace('void ui_task(void *) {','#include "qconsole.inc"\nvoid ui_task(void *) {')
s=s.replace('    static unsigned project_revision = 0, project_changes = 0;','    qconsole();\n    static unsigned project_revision = 0, project_changes = 0;')
s=s.replace('    auto err = audio::write(out_buf, DMA_BUF_LEN);','    auto err = audio::write(out_buf, DMA_BUF_LEN);\n    qmeasure(us, err, transients::active.load());')
app.write_text(s)
console=(Path(__file__).with_name('qconsole.inc')).read_text().replace('Serial.printf(', 'summary_printf(')
console=console.replace('std::max(qmax,us)','std::max<uint32_t>(qmax,us)').replace('std::max(qovermax,us)','std::max<uint32_t>(qovermax,us)').replace('std::max(qactive_max,active)','std::max<uint32_t>(qactive_max,active)').replace('std::min(qcount,16384u)','std::min<uint32_t>(qcount,16384u)')
console=console.replace('track.mute,track.solo,','track.muted,bool(view.solos & (1u<<i)),')
(target/'src/qconsole.inc').write_text(console)
print('Prepared temporary qualification copy',target)
