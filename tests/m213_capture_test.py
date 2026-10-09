from m213_capture import validate, validate_physical
base='[M5] blocks=20672 p50=100 p95=200 p99=300 max={maximum} misses=0 failures=0 timeouts=0\n[M5 memory] ps_before=10 ps_after=10 internal_before=10 internal_after=10 largest_before=10 largest_after=10\n'
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
assert not validate(base.format(maximum=1000).replace('ps_after=10','ps_after=9'))['accepted']
assert not validate(base.format(maximum=1000).replace('largest_after=10','largest_after=0'))['accepted']
print('M213 strict maximum/incomplete/fault/mismatched-trace rejection PASS')
trace=base.format(maximum=4000).replace('timeouts=0','timeouts=0 active_min=16 active_max=16')
trace+='[M212 trace] scenario=2 hash=978631659 events=126480 rng=1386399687 chain_loops=230 repeat_x8=7168\n'
trace+=''.join(f'[M212 residency] track={t} active_frames=5292032 total_frames=5292032\n' for t in range(16))
assert validate(trace,2)['accepted']
assert not validate(trace.replace('rng=1386399687','rng=1'),2)['accepted']
assert not validate(trace.replace('active_min=16','active_min=15'),2)['accepted']
long=base.format(maximum=4000).replace('blocks=20672','blocks=103360')
long+='[M213 monitor] resident=0 corrupt=0 digest=7 free=10 largest=10 internal=10\n'*19
assert validate(long,blocks=103360)['accepted']
assert not validate(long.replace('digest=7','digest=8',1),blocks=103360)['accepted']
assert not validate(long.replace('internal=10','internal=9',1),blocks=103360)['accepted']
assert not validate(long.replace('free=10','free=0',1),blocks=103360)['accepted']
physical='[Q METRICS] blocks=100 stored=100 p99=4400 max=4643 misses=0 failures=0 timeouts=0 nonzero=1 active_max=16\n[Q PLAYBACK] repeat_hits=1 chain_loops=1\n[Q STATE] project_status=PROJECT READY / 0 MISSING\n# COMPLETE; no listening observed\n'
assert validate_physical(physical)['accepted']
for rejected in (physical.replace('4643','4644'),physical.replace('repeat_hits=1','repeat_hits=0'),
                 physical.replace('chain_loops=1','chain_loops=0'),
                 physical.replace('failures=0','failures=1'),physical+'Brownout detector',
                 physical.replace('COMPLETE; no listening observed','incomplete'),
                 physical.replace('0 MISSING','1 MISSING')):
    assert not validate_physical(rejected)['accepted']
print('M213 physical strict reserve/Repeat/restore/fault rejection PASS')
