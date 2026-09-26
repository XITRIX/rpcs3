#!/usr/bin/env python3
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');args=p.parse_args()
def block(s,marker):
    a=s.index(marker);b=s.index('{',a)+1;d=1
    while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
path='rpcs3/Emu/RSX/VK/vkutils/image.cpp'
new=(CORE/path).read_text();old=(HERE/'GTA5BatchReference/Image.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='rpcs3-view-cache-') as d:
    temp=Path(d)
    for name,s in [('baseline',old),('candidate',new)]:
        (temp/'ViewMethods.inc').write_text('\n'.join(block(s,m) for m in ('viewable_image* viewable_image::clone(', 'image_view* viewable_image::get_view(', 'void viewable_image::set_native_component_layout(')))
        exe=temp/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(temp),str(HERE/'VKViewCacheTests.cpp'),'-o',str(exe)]
        if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        subprocess.run(cmd,check=True);subprocess.run([str(exe),name],check=True)
