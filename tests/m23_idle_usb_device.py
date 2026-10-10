"""Real board stress with idle USB Host; never labels this MIDI qualification."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from m23_install_serial import capture
result={}
try:
    for case in ['sample','synth']:
        name='guition_m23_'+case;log='docs/m23/'+case.upper()+'_IDLE_USB_SERIAL.log'
        capture(name,120,log)
        text=(root/log).read_text(errors='replace')
        assert '[M23 MIDI host]' in text and 'connected=0' in text
        assert 'connects=0 disconnects=0 errors=0 rejected=0 overflow=0' in text
        output='docs/m23/'+case+'_idle_usb_results.json'
        subprocess.run([sys.executable,'tests/analyze_m22.py',log,'--output',output]+(['--synth'] if case=='synth' else []),cwd=root,check=True)
        result[case]=dict(scope='IDLE HOST only; no physical MIDI controller/USB traffic',firmware_sha256=hashlib.sha256((root/'.pio/m23-release'/name/'firmware.bin').read_bytes()).hexdigest(),metrics=json.loads((root/output).read_text()))
finally:
    capture('guition_app',8,'docs/m23/PRODUCTION_RESTORED_SERIAL.log')
    (root/'docs/m23/idle_usb_results.json').write_text(json.dumps(result,indent=2)+'\n')
