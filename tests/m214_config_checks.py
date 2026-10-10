"""Check actual production environment inheritance and immutable source guards."""
from pathlib import Path
import configparser
import re
import subprocess

baseline='1c4c926326cc959cad711a74e5c058e08a718f2a'
ini=configparser.ConfigParser(interpolation=None);ini.read('platformio.ini')
def flags(section):
    return re.sub(r'\$\{([^}]+)\.build_flags\}',lambda m:flags(m[1]),ini[section]['build_flags'])
out=Path('.pio/m214-host');out.mkdir(parents=True,exist_ok=True)
for environment,placement,mode in [('guition_app',0,0),('guition_app_nearest_legacy',0,0),
                                 ('guition_app_linear_candidate',1,1)]:
    defined=[f for f in flags('env:'+environment).split() if f.startswith('-DP4SDM_')]
    exe=out/'config.exe'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*defined,
                    f'-DEXPECTED_PLACEMENT={placement}',f'-DEXPECTED_INTERPOLATION={mode}',
                    'tests/m214_config.cpp','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
candidate=flags('env:guition_app_linear_candidate')
for flag in ('INTERPOLATION=1','LINEAR_32BIT=1','LINEAR_MAGNITUDE=1','PCM_READ_CACHE=0','PCM_PLACEMENT=1'):
    assert '-DP4SDM_'+flag in candidate
assert 'STRESS' not in candidate and 'M21' not in candidate
for name in ('model.h','project.h','sample_playback.h','slices.h','transients.h','voice_state.h'):
    old=subprocess.check_output(['git','show',f'{baseline}:src/app/{name}']).decode()
    assert old==Path('src/app',name).read_text(),f'Accepted behavior source changed: {name}'
print('Final flags, standalone production, unchanged scheduler/transport/codec PASS')
for defines in (['-DP4SDM_PCM_PLACEMENT=3'],
                ['-DP4SDM_PCM_PLACEMENT=0','-DP4SDM_M213_LAYOUT=1']):
    invalid=subprocess.run(['g++','-std=c++17',*defines,'tests/m214_config.cpp',
                            '-o',str(out/'invalid.exe')],capture_output=True)
    assert invalid.returncode!=0,'Invalid placement policy was accepted'
subprocess.run(['g++','-std=c++17','-DP4SDM_M213_LAYOUT=1','-DEXPECTED_PLACEMENT=1',
                'tests/m214_config.cpp','-o',str(out/'historical.exe')],check=True)
subprocess.run([str(out/'historical.exe')],check=True)
loader=Path('src/app/samples.cpp').read_text()
assert 'P4SDM_M213_LAYOUT' not in loader
assert 'loaded->pcm.release([](void *base) { heap_caps_free(base); });' in loader
assert 'heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < reserve' in loader
assert 'heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)' in loader
assert 'PCM limit stays 4 MiB' in loader
print('Policy rejection, historical alias, owning-base retirement and reserve guards PASS')
