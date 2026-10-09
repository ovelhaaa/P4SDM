from m213_capture import validate
base='[M5] blocks=20672 p50=1000 p95=2000 p99=3000 max={maximum} misses=0 failures=0 timeouts=0\n[M5 memory] ps_before=1 ps_after=1\n'
assert validate(base.format(maximum=4643))['accepted']
assert not validate(base.format(maximum=4644))['accepted']
assert not validate(base.format(maximum=1000).split('[M5 memory]')[0])['accepted']
assert not validate(base.format(maximum=1000)+'task_wdt: reset')['accepted']
assert not validate(base.format(maximum=1000),2)['accepted']
assert not validate(base.format(maximum=1000).replace('blocks=20672','blocks=100'))['accepted']
assert not validate(base.format(maximum=1000).replace('failures=0','failures=1'))['accepted']
print('M213 strict maximum/incomplete/fault/mismatched-trace rejection PASS')
