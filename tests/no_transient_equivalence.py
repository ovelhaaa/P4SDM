"""Compare actual accepted M18.1 Git sources, events/RNG and encoded V2 bytes."""
import subprocess
import tempfile
from pathlib import Path
baseline = 'bb552f8'
for dependency in ('sample_playback.h', 'slices.h', 'wav.h', 'voice_state.h'):
    accepted = subprocess.check_output(['git', 'show', baseline + ':src/app/' + dependency]).decode()
    assert accepted == (Path('src/app') / dependency).read_text(encoding='utf-8'), dependency
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    for name in ('model.h', 'project.h'):
        source = subprocess.check_output(['git', 'show', baseline + ':src/app/' + name]).decode()
        source = source.replace('namespace app {', 'namespace m18 {').replace('app::', 'm18::').replace('namespace project {', 'namespace legacy {')
        source = source.replace('"model.h"', '"m18_model.h"')
        for dependency in ('sample_playback.h', 'slices.h'):
            source = source.replace('"' + dependency + '"', '"' + (Path('src/app') / dependency).resolve().as_posix() + '"')
        (root / ('m18_' + name)).write_text(source)
    # Reuse every prior performance/codec assertion, extend the same actual
    # baseline simulation to capture/rate/release Repeat with all locks.
    source = Path('tests/no_repeat_equivalence.cpp').read_text()
    injection = '''
        if (i == 280000 || i == 290000 || i == 310000 || i == 350000) {
          app::Kind k = i == 280000 ? app::Kind::PerfRepeatStart : i == 350000 ? app::Kind::PerfRepeatStop : app::Kind::PerfRepeatRate;
          const int rate = i == 280000 ? 8 : i == 290000 ? 2 : i == 310000 ? 4 : 0;
          now.apply({k, 0, rate}); old.apply({m18::Kind(int(k)), 0, rate});
        }
'''
    source = source.replace('for (int i = 0; i < 400000; ++i) {', 'for (int i = 0; i < 400000; ++i) {' + injection)
    source = source.replace('int main() {', '''int main() {
  std::vector<int16_t> fixture(100000);
  for (unsigned i = 0; i < 20; ++i) fixture[3000 + i * 4500] = int16_t(10000 + i * 1000);
  transient::Detector detector(fixture.data(), unsigned(fixture.size()), 1, 44100, {}, transient::Sensitivity::Medium);
  while (!detector.process()) {}
  const auto generated = detector.select(transient::Target::Sixteen);
  assert(generated.count == 16);
''')
    source = source.replace('now.tracks[t].slices.divide({}, 16);', 'now.tracks[t].slices = generated;')
    source = source.replace('old.tracks[t].slices.divide({}, 16);', 'old.tracks[t].slices = generated;')
    source = source.replace('Actual M18 inactive-repeat', 'Actual M18.1 inactive-transient (including Repeat)')
    source = source.replace('Engine M18=', 'Engine M18.1=').replace('Ui M18=', 'Ui M18.1=').replace(' M18.1=', ' M19=')
    source = '#include "' + Path('src/app/transients.h').resolve().as_posix() + '"\n' + source
    source = source.replace('"../src/app/', '"' + (Path('src/app').resolve().as_posix()) + '/')
    (root / 'comparison.cpp').write_text(source)
    binary = root / 'comparison.exe'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + folder, str(root / 'comparison.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('M19 inactive M18.1 event/RNG/Repeat and V2 byte equivalence PASS')
