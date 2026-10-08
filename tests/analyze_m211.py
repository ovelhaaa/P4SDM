"""Validate NEW clean candidate captures; do not infer milestone acceptance.

Historical failing captures remain evidence, never successful safety checks.
--require-acceptance fails until every milestone requirement is demonstrated.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

DEADLINE = 5804.989
LIMIT = 4643.991
CASES = (
    ('nearest_cache_B', 'CACHE_NEAREST_FRACTIONAL', 0, True),
    ('linear_cache_B', 'CACHE_LINEAR_FRACTIONAL', 1, True),
    ('nearest_cache_profile_B', 'CACHE_NEAREST_PROFILE', 0, True),
    ('linear32_cache_B', 'CACHE_LINEAR32_FRACTIONAL', 1, True),
    ('nearest_cache_A', 'CACHE_NEAREST_MIXED', 0, False),
)

def fields(text):
    rows = {}
    # Serial wraps long lines. Preserve continued numeric fields until next tag.
    for match in re.finditer(r'^(\[[^\]\r\n]+\])([^\[]*)', text, re.M):
        rows[match[1]] = {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', match[2])}
    return rows

def clean_case(name, mode, full):
    path = Path(f'docs/GUITION_M211_{name}_SERIAL.log')
    text = path.read_text(errors='replace')
    assert not any(marker in text for marker in
                   ('Guru Meditation', 'task_wdt:', 'stack overflow', 'panic\'ed')), path
    rows = fields(text)
    m, config, mem = rows['[M5]'], rows['[M21 configuration]'], rows['[M5 memory]']
    assert m['blocks'] == 13782 and m['active_max'] == 16, path
    assert m['misses'] == m['failures'] == m['timeouts'] == 0, path
    assert config['mode'] == mode and config['voice_bytes'] == 64, path
    assert config['forced_fractional'] == int(full), path
    assert rows['[M16 project]']['file_bytes'] == 52704, path
    assert rows['[M13 dense]']['active_min'] == 16, path
    assert rows['[M181 x8]']['active_min'] == 16 and rows['[M181 x8]']['full_blocks'] > 500, path
    assert rows['[M19 overlap]']['blocks'] > 0 and rows['[M19 overlap]']['x8_blocks'] > 0, path
    for resource in ('ps', 'internal', 'largest'):
        assert mem[resource+'_before'] == mem[resource+'_after'], path
    frac = rows['[M21 fractional]']
    if full: assert frac['full_blocks'] > 5000, path
    result = dict(log=path.as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  mode=mode, blocks=m['blocks'], active_max=16,
                  p50_us=m['p50'], p95_us=m['p95'], p99_us=m['p99'], max_us=m['max'],
                  headroom_percent=100*(1-m['max']/DEADLINE),
                  reserve_pass=m['max'] <= LIMIT, misses=0, failures=0, timeouts=0,
                  watchdogs=0, panics=0, chain_loops=rows['[M17 chain]']['loops'],
                  fractional_blocks=frac['blocks'], all_fractional_blocks=frac['full_blocks'],
                  repeat_full_blocks=rows['[M181 x8]']['full_blocks'], memory=mem)
    result['timing_and_chain_pass'] = result['reserve_pass'] and result['chain_loops'] > 0
    if '[M21 isolated preparation]' in rows:
        result['isolated_maxima_us'] = rows['[M21 isolated preparation]']
    if 'PROFILE' in name:
        stages = []
        for line in text.splitlines():
            if line.startswith('[M211 block profile]'):
                stage = {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', line)}
                assert stage['blocks'] == 13782, stage
                stages.append(stage)
        assert {s['stage'] for s in stages} == set(range(5)), stages
        result['block_profile'] = stages
        result['profile_memory'] = rows['[M211 profile memory]']
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--require-acceptance', action='store_true')
    args = parser.parse_args()
    results = {key:clean_case(name, mode, full) for key,name,mode,full in CASES}
    record = dict(status='PARTIAL', milestone_accepted=False, production_mode=0,
                  production_cache=False, production_linear32=False,
                  deadline_us=DEADLINE, required_max_us=LIMIT,
                  candidates=results,
                  unresolved=['historical Store fault matching ELF/root cause',
                              'complete equivalent Chain/event coverage',
                              'scenario C complete application qualification',
                              'extended duration/reset and DMA backlog measurements',
                              'fresh physical SD and SAMPLE/SYNTH candidate regressions'])
    target = Path('docs/m21/optimization_device_results.json')
    if args.write: target.write_text(json.dumps(record, indent=2)+'\n')
    else: assert json.loads(target.read_text()) == record, 'Regenerate with --write'
    for key, result in results.items():
        print(f"{key}: max={result['max_us']} us headroom={result['headroom_percent']:.2f}% "
              f"Chain={result['chain_loops']} timing_and_chain_pass={result['timing_and_chain_pass']}")
    print('NEW candidate safety/heap/Repeat checks PASS. M21.1 acceptance: PARTIAL.')
    if args.require_acceptance:
        raise SystemExit('M21.1 NOT ACCEPTED: '+', '.join(record['unresolved']))

if __name__ == '__main__': main()
