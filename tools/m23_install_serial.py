"""Install only reserved M23 files; always restore production firmware.

Build guition_m23_sd_transfer and guition_app first. No formatting or projects.
256-byte checksummed chunks; SHA256 after close/reopen on the physical card.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time,zlib
import serial
ROOT=Path(__file__).resolve().parents[1]
def capture(environment,seconds,path):
    archive=ROOT/'.pio/m23-release'/environment
    if archive.exists():
        import shutil
        shutil.copytree(archive,ROOT/'.pio/build'/environment,dirs_exist_ok=True)
    subprocess.run([sys.executable,'tests/physical_sd/capture.py',environment,str(seconds),str(path)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
def install(pack):
    log=(ROOT/'docs/m23/SD_TRANSFER_SERIAL.log').open('w',encoding='utf-8')
    verified=[];port=None
    try:
        capture('guition_m23_sd_transfer',4,'docs/m23/SD_TRANSFER_BOOT.log')
        port=serial.Serial(port=None,baudrate=115200,timeout=.1);port.dtr=False;port.rts=False;port.port='COM13';port.open()
        def reply(command,seconds=10):
            # HW Serial/JTAG RX has no serial flow control. Pace USB packet
            # bursts even with the enlarged diagnostic RX queue.
            wire=(command+'\n').encode()
            for pos in range(0,len(wire),64):
                port.write(wire[pos:pos+64]);time.sleep(.002)
            port.flush();stop=time.monotonic()+seconds
            while time.monotonic()<stop:
                line=port.readline().decode('utf-8','replace').strip()
                if not line:continue
                log.write(line+'\n');log.flush()
                if line.startswith('M23 ERROR'):raise RuntimeError(line)
                if line.startswith(('M23 READY','M23 COUNT','M23 OFFSET','M23 ACK','M23 VERIFIED','M23 EXISTS')):return line
            raise TimeoutError(command[:100])
        assert reply('PING')=='M23 READY'
        count=int(reply('INVENTORY').split()[-1]);assert count+42<=128,'browser inventory limit; inspect existing M23 files'
        for path in sorted(pack.iterdir()):
            if path.suffix not in ('.wav','.json','.txt'):continue
            name=path.name if path.suffix=='.wav' else 'M23_'+path.name
            data=path.read_bytes();digest=hashlib.sha256(data).hexdigest()
            try:result=reply(f'BEGIN {name} {len(data)} {digest}',30)
            except RuntimeError as error:
                if str(error)!='M23 ERROR collision' or path.suffix=='.wav':raise
                # Documentation can gain a content suffix without breaking kit
                # WAV references. Preserve the earlier documentation verbatim.
                name=f'M23_{path.stem}_{digest[:8]}{path.suffix}'
                result=reply(f'BEGIN {name} {len(data)} {digest}',30)
            if not result.startswith('M23 EXISTS'):
                offset=int(result.split()[-1]);assert offset<=len(data)
                while offset<len(data):
                    chunk=data[offset:offset+256];checksum=zlib.adler32(chunk)
                    result=reply(f'DATA {offset} {checksum} {chunk.hex()}')
                    assert result==f'M23 ACK {offset+len(chunk)}';offset+=len(chunk)
                result=reply('END',30)
            assert result.split()[-1]==digest
            verified.append(dict(filename=name,bytes=len(data),host_sha256=digest,device_reopened_sha256=result.split()[-1]))
            print(f'Physical SD verified {len(verified)}/45: {name}',flush=True)
        (ROOT/'docs/m23/sd_hashes.json').write_text(json.dumps(dict(status='PHYSICAL FILE HASH PASS; loader qualification separate',files=verified),indent=2)+'\n')
    finally:
        if port:port.close()
        log.close()
        capture('guition_app',8,'docs/m23/PRODUCTION_RESTORED_SERIAL.log')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--pack',type=Path,default=ROOT/'assets/m23');a=p.parse_args();install(a.pack)
