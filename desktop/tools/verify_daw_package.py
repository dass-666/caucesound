# Verificación independiente de un paquete para DAW de CAUCESOUND, sin usar el motor.
# Uso: descomprime el paquete, entra en la carpeta y ejecuta: python3 verify_daw_package.py   (necesita: pip install mido)
# Comprueba el MIDI con mido, los WAV con el módulo wave, los hashes del manifiesto y que la suma de stems es la mezcla.
import mido, json, wave, glob, hashlib, sys, struct
m=json.load(open('manifest.json'))
mid=mido.MidiFile(glob.glob('midi/*.mid')[0])
print('mido: tipo',mid.type,'ppq',mid.ticks_per_beat,'pistas',len(mid.tracks),'duracion %.3f s'%mid.length)
tot=0
for i,t in enumerate(mid.tracks):
    on=[x for x in t if x.type=='note_on' and x.velocity>0]; off=[x for x in t if x.type=='note_off' or (x.type=='note_on' and x.velocity==0)]
    print(' ',i,repr(t.name),'notas',len(on),'canales',sorted({x.channel+1 for x in on}))
    tot+=len(on); assert len(on)==len(off)
bpm=mido.tempo2bpm([x.tempo for x in mid.tracks[0] if x.type=='set_tempo'][0])
ts=[x for x in mid.tracks[0] if x.type=='time_signature'][0]
print('notas',tot,'(manifiesto',m['midi']['notes'],') tempo %.3f'%bpm,'compas %d/%d'%(ts.numerator,ts.denominator))
assert tot==m['midi']['notes'] and abs(bpm-m['song']['bpm'])<0.01
for mt in m['midi']['tracks']:
    t=mid.tracks[mt['smfTrack']]
    assert t.name==mt['midiName'], (t.name, mt['midiName'])
    assert len([x for x in t if x.type=='note_on' and x.velocity>0])==mt['notes']
fr=set()
for s in m['stems']+[m['mix']]:
    w=wave.open(s['file']); fr.add((w.getnframes(),w.getframerate(),w.getnchannels(),w.getsampwidth())); w.close()
    assert hashlib.sha256(open(s['file'],'rb').read()).hexdigest()==s['sha256']
print('WAV (frames, Hz, canales, bytes/muestra):',fr,'manifiesto',m['song']['frames'])
assert len(fr)==1 and list(fr)[0][0]==m['song']['frames']
# suma de stems == mezcla / master (24 bits: tolerancia de cuantización)
import array
def read(f):
    w=wave.open(f); n=w.getnframes(); raw=w.readframes(n); w.close()
    return [int.from_bytes(raw[i:i+3],'little',signed=True) for i in range(0,len(raw),3)]
g=10**(m['song']['masterDb']/20)
mix=read(m['mix']['file']); acc=[0]*len(mix)
for s in m['stems']:
    if s['inMix']:
        for i,v in enumerate(read(s['file'])): acc[i]+=v
worst=max(abs(a*g-b) for a,b in zip(acc,mix)); pk=max(abs(b) for b in mix)
print('prueba de nulo independiente (24 bits): diferencia maxima %d LSB, pico de mezcla %d LSB (%.1f dB por debajo)'%(worst,pk, 20*__import__('math').log10(pk/max(worst,1))))
assert worst<=len(m['stems'])+1
print('TODO CORRECTO')
