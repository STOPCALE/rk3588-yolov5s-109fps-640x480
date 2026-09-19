#!/bin/bash
cd ~/testimg
python3 - <<'PY'
import struct, glob
for f in sorted(glob.glob('val/*'))[:3]:
    d = open(f,'rb').read(); i = 2
    while i < len(d):
        if d[i] != 0xFF:
            i += 1
            continue
        m = d[i+1]
        if m in (0xC0, 0xC1, 0xC2):
            h, w = struct.unpack('>HH', d[i+5:i+9])
            print(f, w, h)
            break
        i += 2 + struct.unpack('>H', d[i+2:i+4])[0]
PY
