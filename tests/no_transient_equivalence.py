"""Compare actual accepted M18.1 Git sources, events/RNG and encoded V2 bytes."""
import subprocess
import tempfile
from pathlib import Path
baseline = 'bb552f8'
accepted_main = subprocess.check_output(['git', 'show', baseline + ':src/app_main.cpp']).decode()
current_main = Path('src/app_main.cpp').read_text(encoding='utf-8')
# Normalize only the explicit diagnostic cache alternative. Enabled output and
# transport use actual accepted M21 headers in pcm_cache_checks.py.
current_main = current_main.replace('''#if P4SDM_PCM_READ_CACHE
  v = voice.next(sampler::default_interpolation, &pcm_read_cache[t]);
#else
  v = voice.next();
#endif''', '  v = voice.next();')
def function(source, signature):
    start = source.index(signature)
    opened = source.index('{', start)
    depth = 1
    cursor = opened + 1
    while depth:
        if source[cursor] == '{': depth += 1
        elif source[cursor] == '}': depth -= 1
        cursor += 1
    return source[start:cursor]
for signature in ('static bool app_pcm(int t, int16_t &v)', 'static void app_sample() {'):
    assert function(accepted_main, signature) == function(current_main, signature), signature
assert subprocess.check_output(['git', 'show', baseline + ':synthESP32.ino']).decode().replace('\r\n', '\n') == Path('synthESP32.ino').read_text(encoding='utf-8')
for dependency in ('sample_playback.h', 'slices.h', 'wav.h', 'voice_state.h'):
    accepted = subprocess.check_output(['git', 'show', baseline + ':src/app/' + dependency]).decode()
    current = (Path('src/app') / dependency).read_text(encoding='utf-8')
    if dependency == 'wav.h':
        # M21 changes exactly the reconstruction lookup. Restore only these
        # three known substitutions, then retain the historical full-source
        # assertion. interpolation_checks.py compares actual old/new samples.
        current = current.replace('#include "sample_interpolation.h"\n', '')
        current = current.replace('inline __attribute__((always_inline)) int16_t next(Interpolation interpolation = default_interpolation,\n                                                    PcmReadCache *cache = nullptr)', 'int16_t next()')
        current = current.replace('lookup_valid(sample->data, region, playback.reverse, position,\n                            interpolation, cache)', 'sample->data[frame_index()]')
    assert accepted == current, dependency
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
    source = source.replace('Engine M18=', 'Engine accepted=').replace('Ui M18=', 'Ui accepted=').replace(' M18.1=', ' M19=')
    source = '#include "' + Path('src/app/transients.h').resolve().as_posix() + '"\n' + source
    source = source.replace('"../src/app/', '"' + (Path('src/app').resolve().as_posix()) + '/')
    (root / 'comparison.cpp').write_text(source)
    binary = root / 'comparison.exe'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + folder, str(root / 'comparison.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('M19 inactive M18.1 event/RNG/Repeat and V2 byte equivalence PASS')
