#!/usr/bin/env python3
"""Execute full production vertex-layout/range and texture transfer functions."""
from pathlib import Path
import argparse,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--candidate-root',type=Path,default=CORE);p.add_argument('--sanitize',action='store_true');p.add_argument('--benchmark',action='store_true');p.add_argument('--test-source',type=Path,default=HERE/'MinecraftVertexTests.cpp');p.add_argument('--test-argument',action='append',default=[]);args=p.parse_args()
def block(s,marker):
 a=s.index(marker);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
def extract(root):
 draw=(root/'rpcs3/Emu/RSX/Core/RSXDrawCommands.cpp').read_text();header=(root/'rpcs3/Emu/RSX/Core/RSXVertexTypes.h').read_text();thread=(root/'rpcs3/Emu/RSX/RSXThread.cpp').read_text();prog=(root/'rpcs3/Emu/RSX/Program/program_util.cpp').read_text()
 bufcpp=(root/'rpcs3/Emu/RSX/Common/BufferUtils.cpp').read_text();bufh=(root/'rpcs3/Emu/RSX/Common/BufferUtils.h').read_text()
 return {'index-size':block(bufcpp if 'u32 get_index_type_size(' in bufcpp else bufh,'u32 get_index_type_size('), 'fast-range': block(header,'std::pair<u32, u32> get_required_range(') if 'get_required_range(' in header else 'std::pair<u32,u32> get_required_range(u32 first,u32 count){return calculate_required_range(first,count);}', 'layout':block(header,'class vertex_input_layout')+';','range':block(thread,'std::pair<u32, u32> interleaved_range_info::calculate_required_range('),'type':block(thread if 'u32 get_vertex_type_size_on_host(' in thread else (root/'rpcs3/Emu/RSX/RSXThread.h').read_text(),'u32 get_vertex_type_size_on_host('),'analyse':block(draw,'void draw_command_processor::analyse_inputs_interleaved('),'fill':block(draw,'void draw_command_processor::fill_vertex_layout_state('),'transfer':block(prog,'void fragment_program_texture_config::masked_transfer(')}
with tempfile.TemporaryDirectory(prefix='minecraft-vertex-') as d:
 t=Path(d)
 for k,s in extract(args.candidate_root).items():
  (t/('new-'+k+'.inc')).write_text(('inline ' if k == 'index-size' and 'u32 get_index_type_size(' not in (args.candidate_root/'rpcs3/Emu/RSX/Common/BufferUtils.cpp').read_text() else '__attribute__((noinline)) ' if k == 'index-size' else 'FORCE_INLINE ' if k == 'type' and 'u32 get_vertex_type_size_on_host(' not in (args.candidate_root/'rpcs3/Emu/RSX/RSXThread.cpp').read_text() else '__attribute__((noinline)) ' if k in ['analyse','fill','range','type'] else '')+s+'\n')
  if k != 'fast-range':
   old=(HERE/'MinecraftReference'/(k+'.inc')).read_text();(t/('old-'+k+'.inc')).write_text(('__attribute__((noinline)) ' if k in ['analyse','fill','range','type','index-size'] else '')+old)
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),'-I',str(CORE/'rpcs3'),str(args.test_source),'-o',str(t/'test')]
 if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True);subprocess.run([str(t/'test'),*(['benchmark'] if args.benchmark else []),*args.test_argument],check=True)
