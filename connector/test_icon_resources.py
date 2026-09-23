"""Read-only CI check: every native EXE icon frame equals the supplied ICO.

Parses PE resources without loading or executing the ARM64 app.
"""
import hashlib
import pathlib
import struct
import sys

icon = pathlib.Path(sys.argv[1]).read_bytes()
exe = pathlib.Path(sys.argv[2]).read_bytes()
assert hashlib.sha256(icon).hexdigest() == "189f68a20cb9a8333e575281a14332c355426f43fbd565302faf149fbc42d624"
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
assert icon[:4] == b"\0\0\1\0"
count = u16(icon, 4)
frames = []
for i in range(count):
    entry = 6 + i * 16
    size, offset = u32(icon, entry + 8), u32(icon, entry + 12)
    assert offset + size <= len(icon)
    frames.append((icon[entry:entry + 8], icon[offset:offset + size]))

pe = u32(exe, 0x3c)
assert exe[pe:pe + 4] == b"PE\0\0" and u16(exe, pe + 4) == 0xaa64
opt = pe + 24
assert u16(exe, opt) == 0x20b
sections = []
for i in range(u16(exe, pe + 6)):
    s = opt + u16(exe, pe + 20) + i * 40
    sections.append((u32(exe, s + 12), u32(exe, s + 16), u32(exe, s + 20)))

def file_offset(rva, size):
    for start, raw_size, raw in sections:
        if start <= rva and rva - start + size <= raw_size:
            offset = raw + rva - start
            assert offset + size <= len(exe)
            return offset
    raise AssertionError("resource outside file-backed sections")

resource_rva, resource_size = struct.unpack_from("<II", exe, opt + 112 + 16)
root = file_offset(resource_rva, resource_size)

def leaves(relative=0, path=()):
    assert len(path) <= 3 and relative + 16 <= resource_size
    directory = root + relative
    entries = u16(exe, directory + 12) + u16(exe, directory + 14)
    assert relative + 16 + entries * 8 <= resource_size
    for i in range(entries):
        name, target = struct.unpack_from("<II", exe, directory + 16 + i * 8)
        if name & 0x80000000:
            continue
        current = path + (name,)
        if target & 0x80000000:
            yield from leaves(target & 0x7fffffff, current)
        else:
            assert len(current) == 3 and target + 16 <= resource_size
            rva, size = struct.unpack_from("<II", exe, root + target)
            offset = file_offset(rva, size)
            yield current, exe[offset:offset + size]

resources = dict(leaves())
groups = [(path, data) for path, data in resources.items() if path[0] == 14]
assert groups, "native EXE group icon is missing"
matched = False
for path, group in groups:
    if group[:4] != b"\0\0\1\0" or u16(group, 4) != count:
        continue
    assert len(group) == 6 + count * 14
    for i, (metadata, image) in enumerate(frames):
        entry = 6 + i * 14
        assert group[entry:entry + 8] == metadata
        assert u32(group, entry + 8) == len(image)
        image_id = u16(group, entry + 12)
        assert resources[(3, image_id, path[2])] == image
    matched = True
assert matched, "the supplied ICO was not preserved in the EXE resources"
print(f"PASS ARM64 PE icon: all {count} original icon frames preserved byte-for-byte")
