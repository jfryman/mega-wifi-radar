"""ElevenLabs takes for the Short: python3 eleven.py voice|sfx|music  (into $W/vo, $W/sfx, $W/music).

Never overwrites a file that exists: takes are not reproducible, so a kept take is the only copy.
Key: ~/.config/elevenlabs/api_key. Output mp3_44100_192 needs the Creator tier (128 kbps works below it).
"""
import json, os, re, sys, urllib.error, urllib.request

P = os.path.dirname(os.path.abspath(__file__))
W = os.environ['W']
KEY = open(os.path.expanduser('~/.config/elevenlabs/api_key')).read().strip()
API = 'https://api.elevenlabs.io/v1'
FMT = '?output_format=mp3_44100_192'

ANNOUNCER = 'pNInz6obpgDQGcFmaJgB'   # Adam - Dominant, Firm (premade)
FINE_PRINT = 'cjVigY5qzO86Huf0OWal'  # Eric - Smooth, Trustworthy (premade)

SFX = {   # name: (prompt, seconds)
    'whoosh':  ('fast cartoon swoosh transition, 90s TV commercial', 0.6),
    'whoosh2': ('quick airy whip pan swoosh, bright', 0.5),
    'boing':   ('cartoon spring boing pop, comedic', 0.6),
    'pop':     ('short cartoon bubble pop with sparkle', 0.5),
    'hit':     ('90s synth orchestra hit stab, big and cheesy', 1.2),
    'zap':     ('retro 16-bit video game laser zap', 0.5),
    'ding':    ('bright cartoon ding star twinkle', 0.7),
    'scratch': ('short vinyl record scratch', 0.6),
}

# Track "A", the published one. Describe a style, never name a game: the result must be original.
MUSIC = {'m1': 'High-intensity 16-bit console action game music, early 1990s jungle stage feel. Fast tempo around '
               '160 BPM, punchy FM synth bass driving eighth notes, heroic square-lead melody in a minor key, tight '
               'chiptune drums with snappy snare and fills, rising arpeggios. Starts immediately at full energy, no '
               'intro, builds and ends on a big final hit. Instrumental, original composition.'}


def post(path, body, out, timeout=120):
    if os.path.exists(out):
        print(f'{out}: kept'); return
    req = urllib.request.Request(API + path, data=json.dumps(body).encode(),
                                 headers={'xi-api-key': KEY, 'Content-Type': 'application/json'})
    try:
        data = urllib.request.urlopen(req, timeout=timeout).read()
    except urllib.error.HTTPError as e:
        sys.exit(f'{out}: {e.code} {e.read()[:300]!r}')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'wb').write(data); print(f'{out}: new')


def voice():
    # lines.txt: id|text, v3 delivery tags in [brackets] (v3 acts them, it does not read them)
    for line in open(os.path.join(P, 'lines.txt')):
        if not line.strip() or line.startswith('#'): continue
        n, txt = line.rstrip('\n').split('|', 1)
        post(f'/text-to-speech/{ANNOUNCER}{FMT}', {'text': txt, 'model_id': 'eleven_v3', 'voice_settings':
             {'stability': 0.0, 'similarity_boost': 0.8, 'style': 0.8, 'use_speaker_boost': True}}, f'{W}/vo/l{n}.mp3')
    fine = re.sub(r'\s+', ' ', open(os.path.join(P, 'fine.txt')).read()).strip()
    post(f'/text-to-speech/{FINE_PRINT}{FMT}', {'text': fine, 'model_id': 'eleven_multilingual_v2', 'voice_settings':
         {'stability': 0.7, 'similarity_boost': 0.75, 'style': 0.0, 'speed': 1.2}}, f'{W}/vo/fine.mp3')


def sfx():
    for n, (txt, d) in SFX.items():
        post(f'/sound-generation{FMT}', {'text': txt, 'duration_seconds': d, 'prompt_influence': 0.6}, f'{W}/sfx/{n}.mp3')


def music():
    for n, txt in MUSIC.items():
        post(f'/music{FMT}', {'prompt': txt, 'music_length_ms': 38000, 'model_id': 'music_v1', 'force_instrumental': True},
             f'{W}/music/{n}.mp3', timeout=300)


{'voice': voice, 'sfx': sfx, 'music': music}[sys.argv[1]]()
