"""Isolate cache capacity/fill experiments in a copied diagnostic checkout.

prepare: create/build all cases, without touching the running firmware.
capture: run only after m212_device.py releases COM13; restore normal firmware.
The copy uses 60-second B intervals and counted cache hits/misses (overhead
included). Do not substitute these shorter experiments for the A/B/C captures.
"""
from pathlib import Path
import subprocess,sys,shutil,json
root=Path(__file__).resolve().parents[1]
copy=root/'.pio/m212-matrix'
python=r'C:\.platformio\penv\Scripts\python.exe'
cases=[(size,fill) for size in (16,32,64,128) for fill in (0,1)]
if sys.argv[1]=='prepare':
    copy.mkdir(parents=True,exist_ok=True)
    for folder in ('src','lib'):
        shutil.copytree(root/folder,copy/folder,dirs_exist_ok=True)
    for pattern in ('*.ino','*.h'):
        for file in root.glob(pattern): shutil.copy2(file,copy/file.name)
    for file in ('tests/archive_firmware.py','tests/capture_interpolation.py','tests/physical_sd/capture.py'):
        target=copy/file; target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/file,target)
    ini=(root/'platformio.ini').read_text().replace('boards_dir = boards','boards_dir = '+(root/'boards').as_posix())
    for size,fill in cases:
        ini+=f'\n[env:matrix_{size}_{fill}]\nextends = env:guition_app\nbuild_flags = ${{env:guition_app.build_flags}} -DP4SDM_SAMPLE_PLAYBACK_STRESS=1 -DP4SDM_INTERPOLATION_QUALIFICATION=1 -DP4SDM_M212_SCENARIO=2 -DP4SDM_INTERPOLATION=1 -DP4SDM_LINEAR_32BIT=1 -DP4SDM_PCM_READ_CACHE=1 -DP4SDM_PCM_CACHE_METRICS=1 -DP4SDM_PCM_CACHE_FRAMES={size} -DP4SDM_PCM_CACHE_FILL={fill} -DP4SDM_BLOCK_PROFILE=1\n'
    (copy/'platformio.ini').write_text(ini)
    shutil.copytree(root/'boards',copy/'boards',dirs_exist_ok=True)
    path=copy/'src/app_main.cpp';source=path.read_text()
    source=source.replace('constexpr unsigned capture_blocks = 20672;', 'constexpr unsigned capture_blocks = 10336;')
    source=source.replace('namespace { m212::Trace m212_trace; uint32_t m212_blocks=0; }',
        'namespace { m212::Trace m212_trace; uint32_t m212_blocks=0; uint32_t cache_counts[3]{}; }')
    source=source.replace('if(b<capture_blocks) m212_trace.block(engine,samples::voices);',
        '''if(b<capture_blocks) m212_trace.block(engine,samples::voices);
    if(b+1==capture_blocks) for(const auto &cache:pcm_read_cache) {
      cache_counts[0]+=cache.hits;cache_counts[1]+=cache.misses;cache_counts[2]+=cache.reads;
    }''')
    source=source.replace('for(unsigned t=0;t<16;++t)\n      summary_printf("[M212 residency]',
        '''summary_printf("[M212 cache counts] hits=%u misses=%u reads=%u bytes=%u\\n",cache_counts[0],cache_counts[1],cache_counts[2],unsigned(sizeof(pcm_read_cache)));
    for(unsigned t=0;t<16;++t)
      summary_printf("[M212 residency]''')
    path.write_text(source)
    args=[python,'-m','platformio','run']
    for size,fill in cases: args+=['-e',f'matrix_{size}_{fill}']
    subprocess.run(args,cwd=copy,check=True)
elif sys.argv[1]=='capture':
    results=[]
    try:
        for size,fill in cases:
            label=f'MATRIX_{size}_{fill}';env=f'matrix_{size}_{fill}'
            print(label,flush=True)
            archive=root/'.pio/m212-artifacts'/label
            subprocess.run([sys.executable,'tests/archive_firmware.py',env,str(archive)],cwd=copy,check=True)
            output=root/f'docs/GUITION_M212_{label}_SERIAL.log'
            with (root/f'.pio/m212-{label}-capture.log').open('w',encoding='utf-8') as log:
                subprocess.run([python,'tests/capture_interpolation.py',env,'100',str(output)],cwd=copy,stdout=log,stderr=subprocess.STDOUT,check=True)
            manifest=json.loads((archive/'manifest.json').read_text())
            results.append(dict(case=label,files=manifest['files'],archive=str(archive)))
            (root/'docs/m21/m212_matrix_builds.json').write_text(json.dumps(results,indent=2)+'\n')
    finally:
        with (root/'.pio/m212-matrix-restore.log').open('w',encoding='utf-8') as log:
            subprocess.run([python,'tests/physical_sd/capture.py','guition_app','8','docs/GUITION_M212_MATRIX_RESTORE_SERIAL.log'],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
else: raise SystemExit('prepare or capture')
