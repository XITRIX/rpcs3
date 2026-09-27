#!/usr/bin/env python3
"""Run the production command-buffer setters against a recording-state oracle."""
from pathlib import Path
import argparse,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
CORE = Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", CORE))
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');args=p.parse_args()
def block(s,marker):
    a=s.index(marker);b=s.index('{',a)+1;d=1
    while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
s=(CORE/'rpcs3/Emu/RSX/VK/vkutils/commands.cpp').read_text()
names=['clear_state_cache','bind_pipeline','set_line_width','set_blend_constants','set_depth_bias','set_depth_bounds','set_stencil_write_mask','set_stencil_compare_mask','set_stencil_reference']
with tempfile.TemporaryDirectory(prefix='vk-dynamic-') as temp:
    t=Path(temp);methods='\n'.join(block(s,'void command_buffer::'+n+'(').replace('command_buffer::','') for n in names)
    (t/'DynamicMethods.inc').write_text(methods)
    exe=t/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),'-I',str(CORE/'rpcs3'),str(HERE/'VKDynamicStateTests.cpp'),'-o',str(exe)]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
    # Every direct setter must go through this cache, including overlay resolves.
    vk=CORE/'rpcs3/Emu/RSX/VK'
    for path in list(vk.rglob('*.cpp'))+list(vk.rglob('*.h')):
        if path.name=='commands.cpp':continue
        for token in ['LineWidth','BlendConstants','DepthBias','DepthBounds','StencilWriteMask','StencilCompareMask','StencilReference']:
            assert 'vkCmdSet'+token+'(' not in path.read_text(),str(path)
