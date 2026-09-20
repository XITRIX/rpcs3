#!/usr/bin/env python3
"""Run the production SELF data reader/writer with synthetic file I/O and real AES/zlib."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Crypto/unself.cpp')
parser.add_argument('--fixture', type=Path)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
start = source.index('bool SELFDecrypter::DecryptData()')
end = source.index('\nfs::file SELFDecrypter::MakeElf', start)
header = (ROOT / 'rpcs3/Crypto/unself.h').read_text()
write_start = header.index('\ttemplate<typename EHdr, typename SHdr, typename PHdr>')
write_end = header.index('\n};', write_start)
with tempfile.TemporaryDirectory(prefix='rpcs3-self-tests-') as directory:
    temp = Path(directory)
    (temp / 'SELFDataUnderTest.inc').write_text(source[start:end])
    (temp / 'SELFWriterUnderTest.inc').write_text(header[write_start:write_end])
    executable = temp / 'SELFDecryptionTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), '-I', str(ROOT / 'rpcs3/Crypto'),
               str(HERE / 'SELFDecryptionTests.cpp'), str(ROOT / 'rpcs3/Crypto/aes.cpp'),
               str(ROOT / 'rpcs3/Crypto/aesni.cpp'), '-lz', '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)] + ([str(args.fixture)] if args.fixture else []), check=True)
