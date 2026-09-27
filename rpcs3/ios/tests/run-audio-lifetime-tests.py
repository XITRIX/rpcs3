#!/usr/bin/env python3
"""Run production cellAudio lifecycle methods with tracked guest mappings and failure injection."""
import argparse,os,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=HERE.parents[1]/'Emu/Cell/Modules/cellAudio.cpp');p.add_argument('--sanitize',action='store_true');p.add_argument('--mmapper-source',type=Path,default=HERE.parents[1]/'Emu/Cell/lv2/sys_mmapper.cpp');a=p.parse_args();s=a.source.read_text();h=a.source.with_suffix('.h').read_text()
def method(signature):
    start=s.index(signature);opening=s.index('{',start);depth=1;end=opening+1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
start=s.index('audio_port* cell_audio_thread::open_port()');helpers=s[start:s.index('template <AudioChannelCnt channels, AudioChannelCnt downmix>',start)]
transition=s[s.index('\tclass audio_memory_transition'):s.index('\nerror_code cellAudioInit')]
mmapper=a.mmapper_source.read_text()
unmap=mmapper[mmapper.index('static error_code unmap_shared_memory('):mmapper.index('\n}',mmapper.index('error_code sys_mmapper_unmap_shared_memory_if_matches('))+2]
# Error-code payloads are logging metadata; the fixture retains the returned guest code.
unmap=unmap.replace('return {CELL_EINVAL, addr};','return CELL_EINVAL;')
parts=[unmap,helpers,'namespace {\n'+transition]+[method(x) for x in ['error_code cellAudioInit(', 'error_code cellAudioQuit(', 'error_code cellAudioPortOpen(', 'error_code cellAudioPortClose(']]
with tempfile.TemporaryDirectory(prefix='audio-lifetime-') as tmp:
    out=Path(tmp);(out/'production.inc').write_text('\n'.join(parts));(out/'constants.inc').write_text(h[h.index('enum CellAudioError'):h.index('enum class audio_backend_update')])
    cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(out),str(HERE/'AudioLifetimeTests.cpp'),'-o',str(out/'tests')]
    if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True);subprocess.run([out/'tests'],check=True)
