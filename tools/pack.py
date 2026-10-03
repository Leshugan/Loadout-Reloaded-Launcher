# Packs a resource for the launcher: "LLZ1" + raw size (uint32 LE) + zlib data.
# usage: python3 tools/pack.py <in> <out.llz>
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'wb').write(b'LLZ1' + struct.pack('<I', len(d)) + zlib.compress(d, 9))
