"""Remove the Python binding libraries from the DT_NEEDED lists of C++ programs and libraries.

Message packages list <pkg>__rosidl_generator_py among their libraries, so every C++ consumer links it and, through it,
libpython. No C++ code calls it: a Linux linker drops such entries by default. Linking with --as-needed here is not an option,
because it also drops other unreferenced libraries that class_loader plugins depend on, so exactly these entries go after
installation and everything else keeps the verified link layout. Files under the Python package directory keep theirs.
"""
import struct
import sys
from pathlib import Path

PT_DYNAMIC, PT_LOAD = 2, 1
DT_NULL, DT_NEEDED, DT_STRTAB = 0, 1, 5
SUFFIX = b'__rosidl_generator_py.so'


def needed_names(data):
    """(file offset of the dynamic array, entry count, entries, string table offset), or None if not an ELF64 with one."""
    if data[:5] != b'\x7fELF\x02' or data[5] != 1:
        return None
    program_offset, = struct.unpack_from('<Q', data, 32)
    entry_size, entry_count = struct.unpack_from('<HH', data, 54)
    loads, dynamic = [], None
    for index in range(entry_count):
        kind, _, offset, address, _, file_size, _, _ = struct.unpack_from('<IIQQQQQQ', data, program_offset + index * entry_size)
        if kind == PT_LOAD:
            loads.append((address, offset, file_size))
        elif kind == PT_DYNAMIC:
            dynamic = (offset, file_size)
    if dynamic is None:
        return None
    entries = []
    for index in range(dynamic[1] // 16):
        tag, value = struct.unpack_from('<QQ', data, dynamic[0] + index * 16)
        entries.append((tag, value))
        if tag == DT_NULL:
            break
    strtab = next((value for tag, value in entries if tag == DT_STRTAB), None)
    for address, offset, size in loads:
        if strtab is not None and address <= strtab < address + size:
            return dynamic[0], len(entries), entries, strtab - address + offset
    return None


def name_at(data, offset):
    return data[offset:data.index(b'\0', offset)]


def strip(path):
    data = bytearray(path.read_bytes())
    found = needed_names(data)
    if found is None:
        return 0
    start, count, entries, strings = found
    kept = [e for e in entries if not (e[0] == DT_NEEDED and name_at(data, strings + e[1]).endswith(SUFFIX))]
    removed = count - len(kept)
    if removed:
        kept += [(DT_NULL, 0)] * removed
        for index, entry in enumerate(kept):
            struct.pack_into('<QQ', data, start + index * 16, *entry)
        path.write_bytes(data)
    return removed


def main(install):
    site = install / 'lib/python3.12'
    files = entries = 0
    for path in sorted(install.rglob('*')):
        if path.is_symlink() or not path.is_file() or site in path.parents or path.name.endswith(SUFFIX.decode()):
            continue
        with open(path, 'rb') as handle:
            if handle.read(4) != b'\x7fELF':
                continue
        removed = strip(path)
        files += removed > 0
        entries += removed
    print('removed %d Python binding entries from %d files' % (entries, files))


if __name__ == '__main__':
    main(Path(sys.argv[1]))
