#!/usr/bin/env python3
"""tools/check_zdl_safe.py - static checks that the core stays ZDL-safe.

The Zoom custom-ZDL runtime has no normal C runtime startup / math library.
This scans core/*.c (audio path) for constructs that have frozen real pedals:
double, math-library calls, division, heap, writable statics, 64-bit ints.

Usage: python check_zdl_safe.py [file ...]   (default: core/ampsim.c)
"""
import re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

def strip_comments(src):
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)
    return src

def check(path):
    src = Path(path).read_text(encoding='utf-8')
    code = strip_comments(src)
    problems = []

    if re.search(r'\bdouble\b', code):
        problems.append('uses double')
    for fn in ('sinf','cosf','tanf','logf','powf','expf','sqrtf','atanf','fabsf','floorf','ceilf'):
        if re.search(r'\b' + fn + r'\s*\(', code):
            problems.append(f'uses {fn}()')
    # division: '/' not preceded/followed by '/' or '*'
    if re.search(r'[^/*]/[^/*]', code):
        problems.append('uses division')
    for fn in ('malloc','calloc','realloc','free'):
        if re.search(r'\b' + fn + r'\s*\(', code):
            problems.append(f'uses {fn}()')
    if re.search(r'\blong\s+long\b|\blong\s+double\b', code):
        problems.append('uses 64-bit/long double')
    # writable file-scope statics (non-const): a line of the form
    #   static <type> <name>;
    # at column 0 (functions start with '(' so they never match).
    var_re = re.compile(
        r'^\s*static\s+(?!const)\s*'
        r'(?:float|int|unsigned|char|short|long|uint8_t|uint16_t|uint32_t|size_t|double)'
        r'\s+\w+\s*;', re.M)
    if var_re.search(code):
        problems.append('file-scope writable static variable')
    # file-scope writable arrays:  static <type> <name>[ ... ]  (not const)
    arr_re = re.compile(
        r'^\s*static\s+(?!const)\s*'
        r'(?:float|int|unsigned|char|short|long|uint8_t|uint16_t|uint32_t|size_t|double)'
        r'\s+\w+\s*\[', re.M)
    if arr_re.search(code):
        problems.append('file-scope writable array static')

    if problems:
        print(f'FAIL {path}: {", ".join(problems)}')
        return 1
    print(f'ok   {path}: ZDL-safe (no double/mathlib/division/heap/writable static)')
    return 0

def main(argv):
    files = argv[1:] or [str(ROOT / 'core' / 'ampsim.c')]
    rc = 0
    for f in files:
        rc |= check(f)
    return rc

if __name__ == '__main__':
    sys.exit(main(sys.argv))
