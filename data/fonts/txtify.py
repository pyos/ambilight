#!/usr/bin/env python3
import os
import os.path
import json

root = os.path.dirname(__file__) or os.curdir

for file in os.listdir(root):
    if not file.endswith('.json'):
        continue
    with open(os.path.join(root, file)) as fd:
        data = json.load(fd)
    with open(os.path.join(root, file)[:-5] + '.txt', 'w') as fd:
        fd.write('# Character map for texture {}\n'.format(file[:-5] + '.png'))
        data['kind'] = 'bold-italic' if data['bold'] and data['italic'] \
            else 'bold' if data['bold'] \
            else 'italic' if data['italic'] \
            else 'normal'
        fd.write('{name}, {size}, {kind}\n'.format_map(data))
        fd.write('# Format: ordinal, x, y, width, height, origin x, origin y, advance\n')
        for char, desc in data["characters"].items():
            desc['codepoint'] = ord(char)
            fd.write('{codepoint:>5}, {x:>5}, {y:>5}, {width:>5}, {height:>5}, '
                     '{originX:>5}, {originY:>5}, {advance:>5}\n'.format_map(desc))
