from m213_capture import validate, validate_physical
base='[M5] blocks=20672 p50=100 p95=200 p99=300 max={maximum} misses=0 failures=0 timeouts=0\n[M5 memory] ps_before=1 ps_after=1\n'
assert validate(base.format(maximum=4643))['accepted']
assert not validate(base.format(maximum=4644))['accepted']
assert not validate(base.format(maximum=1000).split('[M5 memory]')[0])['accepted']
assert not validate(base.format(maximum=1000)+'task_wdt: reset')['accepted']
assert not validate(base.format(maximum=1000),2)['accepted']
assert not validate(base.format(maximum=1000).replace('blocks=20672','blocks=100'))['accepted']
assert not validate(base.format(maximum=1000).replace('failures=0','failures=1'))['accepted']
assert not validate(base.format(maximum=1000)+'[AUDIO] I2S init\n[AUDIO] I2S init\n')['accepted']
assert not validate(base.format(maximum=1000)+'[M213 monitor] corrupt=1\n')['accepted']
assert not validate(base.format(maximum=200))['accepted']
assert not validate(base.format(maximum=1000)+'E BOD: Brownout detector was triggered')['accepted']
print('M213 strict maximum/incomplete/fault/mismatched-trace rejection PASS')
physical='[Q METRICS] blocks=100 stored=100 p99=4400 max=4643 misses=0 failures=0 timeouts=0 nonzero=1 active_max=16\n[Q PLAYBACK] repeat_hits=1 chain_loops=1\n[Q STATE] project_status=PROJECT READY / 0 MISSING\n# COMPLETE; no listening observed\n'
assert validate_physical(physical)['accepted']
for rejected in (physical.replace('4643','4644'),physical.replace('repeat_hits=1','repeat_hits=0'),
                 physical.replace('chain_loops=1','chain_loops=0'),
                 physical.replace('failures=0','failures=1'),physical+'Brownout detector',
                 physical.replace('COMPLETE; no listening observed','incomplete'),
                 physical.replace('0 MISSING','1 MISSING')):
    assert not validate_physical(rejected)['accepted']
print('M213 physical strict reserve/Repeat/restore/fault rejection PASS')
