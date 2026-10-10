"""Explicit source audit utility; refresh only after reviewing upstream licenses.

The build uses m23_sources.json, never this mutable acquisition step.
"""
from pathlib import Path
import hashlib,json,re,subprocess,urllib.request
ROOT=Path(__file__).resolve().parents[1]
def run():
    cache=ROOT/'.pio/m23-source-cache';cache.mkdir(exist_ok=True)
    lock=[]
    acoustic=[
      ('SNARE_ACOUSTIC_01','snare','caisse-claire-6-s2309',None),
      ('SNARE_ACOUSTIC_02','snare','caisse-claire-7-s2310',None),
      ('HAT_CLOSED_ACOUSTIC_01','hat_closed','charleston-fermee-6-s2301',None),
      ('HAT_CLOSED_ACOUSTIC_02','hat_closed','charleston-1-s2295',None),
      ('TOM_LOW','tom','tom-grave-1-s2338',None),
      ('TOM_MID','tom','tom-medium-1-s2333',None),
      ('TOM_HIGH','tom','tom-aigu-1-s2329',None),
      ('TAMBOURINE_01','shaker','tambourin-s0463',.75),
      ('PERC_TRIANGLE','percussion','triangle-3-s1689',None),
      ('PERC_AGOGO','percussion','cartoon-agogo-2-s2262',None),
      ('PERC_DJEMBE','percussion','djembe-main-a-plat-au-centre-s0560',.8)]
    for name,category,slug,seconds in acoustic:
        url=f'https://lasonotheque.org/{slug}.html'
        html=urllib.request.urlopen(url,timeout=60).read().decode('utf-8')
        if 'CC0' not in html:raise ValueError('No individual CC0 evidence: '+url)
        download=re.search(r'"contentUrl":"(https://lasonotheque.org/UPLOAD/[^\"]+\.wav)"',html)
        if not download:raise ValueError('No public WAV URL: '+url)
        data=urllib.request.urlopen(download[1],timeout=60).read()
        filename='M23_'+name+'.wav';cached=slug+'.wav';(cache/cached).write_bytes(data)
        lock.append(dict(filename=filename,category=category,description='Recorded acoustic '+category+'; '+slug,cache_name=cached,download_url=download[1],source_url=url,source_type='acoustic_recording',original_author='Joseph Sardin / LaSonotheque',license='CC0-1.0',license_evidence_url=url,source_sha256=hashlib.sha256(data).hexdigest(),first_hit_seconds=seconds))
        print('Audited CC0',filename,flush=True)
    repository=ROOT/'.pio/m23-uzu'
    commit=subprocess.check_output(['git','-C',str(repository),'rev-parse','HEAD'],text=True).strip()
    assert 'public domain' in (repository/'LICENSE').read_text()
    groups=[('kick','bd',['KICK_ELECTRO_01','KICK_ELECTRO_02','KICK_ELECTRO_03']),('snare','sd',['SNARE_ELECTRO_01','SNARE_ELECTRO_02']),('clap','cp',['CLAP_01','CLAP_02']),('rim','rim',['RIM_01','RIM_02']),('hat_closed','hh',['HAT_CLOSED_ELECTRO_01','HAT_CLOSED_ELECTRO_02']),('hat_open','oh',['HAT_OPEN_01','HAT_OPEN_02']),('shaker','sh',['SHAKER_01']),('percussion','cb',['PERC_COWBELL'])]
    for category,folder,names in groups:
        for name,path in zip(names,sorted((repository/folder).glob('*.wav'))):
            data=path.read_bytes();rel=path.relative_to(repository).as_posix();cached='uzu_'+path.name
            (cache/cached).write_bytes(data)
            lock.append(dict(filename='M23_'+name+'.wav',category=category,description='Analog processed electronic '+category,cache_name=cached,download_url=f'https://raw.githubusercontent.com/tidalcycles/uzu-drumkit/{commit}/{rel}',source_url=f'https://github.com/tidalcycles/uzu-drumkit/blob/{commit}/{rel}',source_type='external_electronic',original_author='Switch Angel, Mot4i, Ludens (uzu-drumkit contributors)',license='Unlicense',license_evidence_url=f'https://github.com/tidalcycles/uzu-drumkit/blob/{commit}/LICENSE',commit=commit,source_sha256=hashlib.sha256(data).hexdigest()))
    assert len(lock)==26,len(lock)
    (ROOT/'tools/m23_sources.json').write_text(json.dumps(lock,indent=2)+'\n')
if __name__=='__main__':run()
