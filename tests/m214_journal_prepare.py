"""Instrument a private copy of the complete original A workload, not production."""
from pathlib import Path
import shutil

root=Path('.pio/m214-journal');root.mkdir(parents=True,exist_ok=True)
for folder in ('src','lib','boards'): shutil.copytree(folder,root/folder,dirs_exist_ok=True)
for pattern in ('*.ino','*.h'):
    for file in Path('.').glob(pattern): shutil.copy2(file,root/file.name)
shutil.copy2('tests/m214_journal.inc',root/'src/m214_journal.inc')
source=Path('src/app_main.cpp').read_text()
source=source.replace('#include "app/model.h"','#include "app/model.h"\n#include "app/m212_fixture.h"',1)
source=source.replace('void trigger(app::TriggerEvent event,', '#include "m214_journal.inc"\nvoid trigger(app::TriggerEvent event,',1)
source=source.replace('engine.apply(', 'm214_journal_apply(')
source=source.replace('  voice_routing.trigger(t, sample);', '''  if(m214_recording && captured.load()<capture_blocks)
    m214_trace.event(event,ratchet,sample,engine.rng);
  voice_routing.trigger(t, sample);''',1)
source=source.replace('    project::snapshot_part(engine, *projects::staging, part);',
                       '    m214_record(2,part);\n    project::snapshot_part(engine, *projects::staging, part);',1)
source=source.replace('      project::apply_part(engine, *projects::staging, part);',
                       '      m214_record(3,part);\n      project::apply_part(engine, *projects::staging, part);',1)
source=source.replace('      engine.selected_pattern = engine.playing_pattern = 15;',
                       '      m214_record(5);\n      engine.selected_pattern = engine.playing_pattern = 15;',1)
source=source.replace('      engine.bpm = 240;', '      m214_record(6);\n      engine.bpm = 240;',1)
source=source.replace('      m214_journal_apply(c);',
                       '      if(transient_applied) m214_record(4,c.track);\n      m214_record(1,0,c);\n      engine.apply(c);',1)
source=source.replace('      if (c.track >= 16)\n        continue;\n      else if (c.kind == app::Kind::GateRelease)',
                       '      if (c.track >= 16)\n        continue;\n      else if ((m214_record(7,0,c),c.kind == app::Kind::GateRelease))',1)
source=source.replace('    unsigned b = captured.load(std::memory_order_relaxed);', '''    unsigned b = captured.load(std::memory_order_relaxed);
    if(b<capture_blocks) {
      m214_trace.block(engine,samples::voices);
      for(unsigned t=0;t<16;++t) {
        const auto &v=samples::voices[t];
        m214_trace.add(voice_routing.pcm(t));m214_trace.add(v.remaining);
        m214_trace.add(v.attack);m214_trace.add(v.release_left);
        m214_trace.add(v.releasing);m214_trace.add(v.sequenced);
      }
    }''',1)
source=source.replace('  const bool natural = voice.active && !voice.releasing;',
    '  const bool natural = voice.active && !voice.releasing;\n  if(m214_recording && captured.load()<capture_blocks) m214_trace.active_frames[t]+=natural;',1)
source=source.replace('  xTaskCreatePinnedToCore(audio_worker,', '  m214_journal_begin();\n  xTaskCreatePinnedToCore(audio_worker,',1)
# Dump after the ordinary UI/memory summaries, at the very end of the run.
source=source.replace('    audio_summary_ready.store(true, std::memory_order_release);',
                       '    audio_summary_ready.store(true, std::memory_order_release);\n    m214_journal_dump();',1)
source=source.replace('                     unsigned(sampler::default_interpolation));\n#endif\n    }',
                       '                     unsigned(sampler::default_interpolation));\n#endif\n      m214_ui_complete.store(true,std::memory_order_release);\n    }',1)
(root/'src/app_main.cpp').write_text(source)
ini=Path('platformio.ini').read_text()+'''
[env:guition_m214_A_journal]
extends = env:guition_ui20_stress
build_flags = ${env:guition_ui20_stress.build_flags} -DP4SDM_INTERPOLATION=1 -DP4SDM_LINEAR_32BIT=1 -DP4SDM_LINEAR_MAGNITUDE=1 -DP4SDM_PCM_READ_CACHE=0 -DP4SDM_PCM_PLACEMENT=1 -DP4SDM_M213_FIXTURE_LAYOUT=1 -DP4SDM_INTERPOLATION_QUALIFICATION=1
'''
(root/'platformio.ini').write_text(ini)
print('Original A command journal prepared; production source unchanged')
