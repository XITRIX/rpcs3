#!/usr/bin/env python3
"""Execute the production mixed mip-chain path with CPU/GPU ownership fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
p.add_argument('--negative-control', choices=['stride', 'ownership', 'missing', 'uncached', 'stale'])
a = p.parse_args()
s = (ROOT / 'rpcs3/Emu/RSX/Common/texture_cache.h').read_text()
begin = s.rindex('\t\ttemplate <', 0, s.index('sampled_image_descriptor gather_mipmaps_with_cpu_base('))
end = s.index('\n#endif', begin)
body = s[begin:end]
if a.negative_control == 'stride':
    body = body.replace('level_attr.address += level_attr.pitch * level_attr.height;', 'level_attr.address += level_attr.width * 4 * level_attr.height;')
elif a.negative_control == 'ownership':
    body = body.replace('get_intersecting_set(base_range.to_page_range()).has_flushables', 'false')
elif a.negative_control == 'uncached':
    body = body.replace('desc.mixed_mipmap_source_tags.push_back(m_cache_update_tag.load());', 'desc.do_not_cache = true; desc.mixed_mipmap_source_tags.push_back(m_cache_update_tag.load());')
elif a.negative_control == 'missing':
    body = body.replace('if (!image.validate() || image.upload_context', 'if (false && (!image.validate() || image.upload_context').replace('level, false, attr))', 'level, false, attr)))')
def method(signature, terminator):
    start = s.index(signature)
    return s[start:s.index(terminator, start)]
source_check = method('\t\t\tbool same_mipmap_sources(', '\n\t\t\tusing section_array_type')
create = method('\t\timage_view_type create_temporary_subresource(', '\n\t\tvoid release_uncached_temporary_subresources')
if a.negative_control == 'stale':
    source_check = source_check.replace('mixed_mipmap_source_tags.size() != other.mixed_mipmap_source_tags.size() ||', 'false && (mixed_mipmap_source_tags.size() != other.mixed_mipmap_source_tags.size() ||').replace('other.mixed_mipmap_source_tags.begin()))', 'other.mixed_mipmap_source_tags.begin())))')
with tempfile.TemporaryDirectory(prefix='mixed-mipmap-tests-') as directory:
    d = Path(directory)
    (d / 'MixedMipmapsUnderTest.inc').write_text(body)
    (d / 'MixedMipmapSourcesUnderTest.inc').write_text(source_check)
    (d / 'MixedMipmapCacheUnderTest.inc').write_text(create)
    exe = d / 'test'
    cmd = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(d), str(HERE / 'MixedMipmapsTests.cpp'), '-o', str(exe)]
    if a.sanitize:
        cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(cmd, check=True)
    result = subprocess.run([str(exe)], capture_output=bool(a.negative_control))
    if a.negative_control:
        assert result.returncode != 0 and b'Assertion' in result.stderr, result
        print('Negative control rejected:', a.negative_control)
    else:
        result.check_returncode()
