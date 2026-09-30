#!/usr/bin/env python3
"""Attribute a macOS `sample` profile of one thread to x86 modules, through FEX's JIT map.

Run the game with FEX_BLOCKJITNAMING=1 FEX_DISKCACHE=0 (scripts/play passes both through) so FEX
writes /tmp/perf-<pid>.map, then:

    sample <pid> 5 -file out.txt
    tools/fexprof.py out.txt /tmp/perf-<pid>.map [Thread_NNN]

Without a thread, the one with the most samples in JIT code is used. Prints self time per x86
module (host functions are listed as "host: <name>") and the hottest translated blocks, as
module+offset for llvm-symbolizer.
"""
import bisect
import collections
import re
import sys


def busiest_jit_thread(lines):
    """The thread with the most self samples in JIT code."""
    threads = {m.group(1) for m in (re.match(r'^    \d+ (Thread_\d+)', l) for l in lines) if m}
    def jit_samples(thread):
        return sum(count for text, count in self_samples(lines, thread).items() if 'unknown binary' in text)
    return max(threads, key=jit_samples)


def self_samples(lines, thread):
    start = next(i for i, l in enumerate(lines) if re.match(r'^\s+\d+ ' + re.escape(thread) + r'\b', l))
    nodes = []
    for line in lines[start + 1:]:
        if re.match(r'^    \d+ Thread_', line) or not line.strip():
            break
        m = re.match(r'^([ +!:|]*)(\d+) (.*)$', line)
        if m:
            nodes.append((len(m.group(1)), int(m.group(2)), m.group(3)))
    result = collections.Counter()
    for i, (depth, count, text) in enumerate(nodes):
        children = 0
        for d, c, _ in nodes[i + 1:]:
            if d <= depth:
                break
            if d == depth + 2:
                children += c
        if count > children:
            result[text] += count - children
    return result


def main():
    sample_file, map_file = sys.argv[1], sys.argv[2]
    lines = open(sample_file).read().split('\n')
    thread = sys.argv[3] if len(sys.argv) > 3 else busiest_jit_thread(lines)
    blocks = []
    for line in open(map_file):
        parts = line.rstrip('\n').split(' ', 2)
        if len(parts) == 3:
            blocks.append((int(parts[0], 16), int(parts[1], 16), parts[2]))
    # By address only: the sort is stable, so where FEX reused an address after clearing its
    # cache, the newest translation (last in the file) stays last and bisect finds it.
    blocks.sort(key=lambda b: b[0])
    starts = [b[0] for b in blocks]

    by_module, by_block = collections.Counter(), collections.Counter()
    samples = self_samples(lines, thread)
    total = sum(samples.values())
    for text, count in samples.items():
        m = re.search(r'\[0x([0-9a-f]+)\]', text)
        if m and 'unknown binary' in text:
            addr = int(m.group(1), 16)
            i = bisect.bisect_right(starts, addr) - 1
            name = blocks[i][2] if i >= 0 and addr < blocks[i][0] + blocks[i][1] else 'jit (unmapped)'
            module = name.split('+')[0]
            by_module[module] += count
            by_block[re.sub(r' \(0x[0-9a-f]+\)$', '', name)] += count
        else:
            by_module['host: ' + re.sub(r'\s+\(in .*', '', text).strip()[:70]] += count
    print(f'{thread}: {total} self samples')
    for name, count in by_module.most_common(20):
        print(f'{count:6d} {100 * count / total:5.1f}% {name}')
    print('--- hottest blocks')
    for name, count in by_block.most_common(15):
        print(f'{count:6d} {name}')


if __name__ == '__main__':
    main()
