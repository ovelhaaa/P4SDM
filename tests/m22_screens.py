"""Publish reproducible, labeled contact sheets from production framebuffer dumps."""
from pathlib import Path
import subprocess
import sys
from PIL import Image,ImageDraw

root=Path('.pio');out=Path('docs/m22');out.mkdir(exist_ok=True)
for revision,destination in [('72a9178','m22-before'),(None,'m22-after')]:
    subprocess.run([sys.executable,'tests/render_ui.py','--output',str(root/destination)]+(['--revision',revision] if revision else []),check=True)

views=[('SEQ','page_0'),('TRACK MAIN','page_1'),('TRACK TONE','page_11'),
       ('SAMPLE BROWSER','page_2'),('SAMPLE PLAYBACK','page_12'),('SAMPLE SLICE','page_13'),
       ('SAMPLE AUTO','page_17'),('PATTERN SELECT','page_4'),('PATTERN STEP','page_5'),
       ('LOCKS BASIC','page_7'),('LOCKS TONE','page_10'),('LOCKS SAMPLE','page_14'),
       ('PERFORM PATTERNS','page_15'),('PERFORM MIX','mix'),('PERFORM REPEAT','page_16'),
       ('PERFORM CHAIN','page_8'),('PROJECT HOME','page_6'),('PROJECT CONFIRM','confirm'),
       ('PROJECT LOAD','load'),('MISSING / LONG WAV','missing_sample'),('SYNTH DISABLED','disabled_sample')]
# Page enum is read below so this script cannot silently label a different view.
source=Path('src/app/model.h').read_text();body=source.split('enum class Page {',1)[1].split('}',1)[0]
names=[n.strip().split('=')[0].strip() for n in body.split(',') if n.strip()]
mapping={'SEQ':'Sequence','TRACK MAIN':'Track','TRACK TONE':'Tone','SAMPLE BROWSER':'Sample','SAMPLE PLAYBACK':'SamplePlayback','SAMPLE SLICE':'SampleSlice','SAMPLE AUTO':'AutoSlice','PATTERN SELECT':'Pattern','PATTERN STEP':'Step','LOCKS BASIC':'Locks','LOCKS TONE':'ToneLocks','LOCKS SAMPLE':'SampleLocks','PERFORM PATTERNS':'Performance','PERFORM REPEAT':'PerformanceRepeat','PERFORM CHAIN':'Chain','PROJECT HOME':'Project'}
for n,(title,name) in enumerate(views):
    if title in mapping: views[n]=(title,'page_'+str(names.index(mapping[title])))

def sheet(rows,folders,path):
    cols=len(folders);width=400*cols;canvas=Image.new('RGB',(width,48+len(rows)*264),'#101010');draw=ImageDraw.Draw(canvas)
    draw.text((8,8),'SIMULATED PRODUCTION FRAMEBUFFER OUTPUT - NOT LCD PHOTOGRAPHS',fill='white')
    draw.text((8,26),'800 x 480 source; fixed maximal values and long/missing WAV fixtures',fill='white')
    for row,(title,name) in enumerate(rows):
        for col,folder in enumerate(folders):
            image=Image.open(root/folder/(name+'.ppm'));image.thumbnail((400,240))
            y=48+row*264;draw.text((col*400+8,y+3),title+' / '+folder,fill='white');canvas.paste(image,(col*400,y+24))
    canvas.save(path)
sheet(views,['m22-after'],out/'sections.png')
sheet([views[n] for n in (0,3,5,13,14,17)],['m22-before','m22-after'],out/'before_after.png')
print('M22 before/after and full subtab host-render evidence PASS')
