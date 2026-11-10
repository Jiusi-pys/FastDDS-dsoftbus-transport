"""Host test for drop_python_needed.py on a minimal ELF64 file: only the Python binding entries may go."""
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'integrations/ros2/target/python'))
import drop_python_needed as tool  # noqa: E402


def make_elf(names, extra=((14, 7),)):
    """One PT_LOAD over the file, a PT_DYNAMIC, and DT_NEEDED entries followed by extra entries and a terminator."""
    strings = b'\0'
    offsets = []
    for name in names:
        offsets.append(len(strings))
        strings += name.encode() + b'\0'
    header, programs = 64, 56 * 2
    string_offset = header + programs
    dynamic_offset = string_offset + len(strings)
    dynamic_offset += -dynamic_offset % 8
    entries = [(tool.DT_NEEDED, o) for o in offsets] + [(tool.DT_STRTAB, 0x1000 + string_offset)] + list(extra) + [(tool.DT_NULL, 0)]
    body = b''.join(struct.pack('<QQ', *e) for e in entries)
    total = dynamic_offset + len(body)
    data = bytearray(total)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<Q', data, 32, header)
    struct.pack_into('<HH', data, 54, 56, 2)
    struct.pack_into('<IIQQQQQQ', data, header, tool.PT_LOAD, 5, 0, 0x1000, 0x1000, total, total, 0x1000)
    struct.pack_into('<IIQQQQQQ', data, header + 56, tool.PT_DYNAMIC, 6, dynamic_offset, 0x1000 + dynamic_offset, 0, len(body), len(body), 8)
    data[string_offset:string_offset + len(strings)] = strings
    data[dynamic_offset:total] = body
    return bytes(data)


def needed(path):
    data = path.read_bytes()
    _, _, entries, strings = tool.needed_names(data)
    return [tool.name_at(data, strings + value).decode() for tag, value in entries if tag == tool.DT_NEEDED]


def main():
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / 'node'
        path.write_bytes(make_elf(['librcl.so', 'libstd_msgs__rosidl_generator_py.so', 'libc++_shared.so', 'libbuiltin_interfaces__rosidl_generator_py.so']))
        size = path.stat().st_size
        assert tool.strip(path) == 2, 'both binding entries are removed'
        assert needed(path) == ['librcl.so', 'libc++_shared.so'], 'other entries stay, in order'
        assert path.stat().st_size == size, 'the file keeps its size and offsets'
        _, _, entries, _ = tool.needed_names(path.read_bytes())
        assert (14, 7) in entries and any(tag == tool.DT_STRTAB for tag, _ in entries), 'other tags stay'
        assert tool.strip(path) == 0, 'a second pass changes nothing'

        untouched = Path(folder) / 'library'
        original = make_elf(['librcl.so'])
        untouched.write_bytes(original)
        assert tool.strip(untouched) == 0 and untouched.read_bytes() == original, 'a file with no binding entry is not rewritten'
        similar = Path(folder) / 'similar'
        similar.write_bytes(make_elf(['librosidl_generator_py_helper.so']))
        assert tool.strip(similar) == 0, 'only the exact suffix matches'
        text = Path(folder) / 'script'
        text.write_bytes(b'#!/bin/sh\n')
        assert tool.strip(text) == 0, 'not an ELF file'

        tree = Path(folder) / 'install'
        site = tree / 'lib/python3.12/site-packages/std_msgs'
        site.mkdir(parents=True)
        program = tree / 'lib/talker'
        extension = site / 'std_msgs_s__rosidl_typesupport_c.so'
        binding = tree / 'lib/libstd_msgs__rosidl_generator_py.so'
        for item in (program, extension, binding):
            item.write_bytes(make_elf(['libstd_msgs__rosidl_generator_py.so', 'libpython3.12.so.1.0']))
        tool.main(tree)
        assert needed(program) == ['libpython3.12.so.1.0'], 'a C++ program loses the binding entry'
        assert len(needed(extension)) == 2, 'an extension module keeps it'
        assert len(needed(binding)) == 2, 'a binding library keeps its own entries'
    print('drop_python_needed tests passed')


if __name__ == '__main__':
    main()
