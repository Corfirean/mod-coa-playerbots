import argparse
import ctypes
import json
from pathlib import Path
import struct
import subprocess
import mpyq


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, default=Path("C:/games/Ascension"))
    parser.add_argument("--dbc", type=Path, default=Path("C:/games/CoA Server 2/Data/dbc/ItemDisplayInfo.dbc"))
    parser.add_argument("--mysql", default="C:/games/CoA Server 2/mysql/bin/mysql.exe")
    parser.add_argument("--defaults-file", default="C:/games/CoA Server 2/mysql/admin-client.ini")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--decrypt-library", type=Path)
    args = parser.parse_args()
    if args.decrypt_library:
        library = ctypes.CDLL(str(args.decrypt_library.resolve()))
        decrypt = library.decrypt_table
        decrypt.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t, ctypes.c_uint32,
                            ctypes.POINTER(ctypes.c_uint32)]
        def fast_decrypt(archive, data, key):
            buffer = ctypes.create_string_buffer(data, len(data))
            table = (ctypes.c_uint32 * 1280)(*[archive.encryption_table[i] for i in range(1280)])
            decrypt(ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint32)), len(data) // 4, key, table)
            return buffer.raw
        mpyq.MPQArchive._decrypt = fast_decrypt
    raw = args.dbc.read_bytes()
    magic, count, fields, size, strings = struct.unpack_from("<4s4I", raw)
    if magic != b"WDBC" or fields < 3:
        raise ValueError("Unexpected ItemDisplayInfo format")
    string_start = 20 + count * size
    rows = {}
    for index in range(count):
        row = struct.unpack_from("<" + "I" * fields, raw, 20 + index * size)
        names = []
        for offset in row[1:3]:
            end = raw.find(b"\x00", string_start + offset)
            names.append(raw[string_start + offset:end].decode("utf-8", errors="replace"))
        rows[row[0]] = names
    query = "SELECT DISTINCT displayid,InventoryType FROM acore_world.item_template WHERE class=4 AND InventoryType IN (1,3)"
    result = subprocess.run([args.mysql, "--defaults-extra-file=" + args.defaults_file, "--batch",
                             "--skip-column-names", "-e", query], capture_output=True, text=True, check=True)
    displays = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    candidates = {}
    for display, slot in displays:
        names = rows.get(display)
        if names is None:
            candidates[display] = [set()]
            continue
        groups = []
        for name in filter(None, names):
            base = "Item\\ObjectComponents\\" + ("Head" if slot == 1 else "Shoulder") + "\\" + name
            base = base.removesuffix(".mdx").removesuffix(".m2")
            group = {base + ".m2"}
            if slot == 1:
                for race in ("Hu", "Or", "Dw", "Ni", "Sc", "Ta", "Gn", "Tr", "Be", "Dr"):
                    for gender in ("M", "F"):
                        group.add(base + "_" + race + gender + ".m2")
            groups.append(group)
        candidates[display] = groups
    needed = set().union(*(group for groups in candidates.values() for group in groups))
    found = set()
    archives = sorted(args.client.joinpath("Data").rglob("*.MPQ"))
    for archive_path in archives:
        archive = mpyq.MPQArchive(str(archive_path), listfile=False)
        hashes = {(entry.hash_a, entry.hash_b) for entry in archive.hash_table if entry.block_table_index < 0xFFFFFFFE}
        for name in needed - found:
            if (archive._hash(name, "HASH_A"), archive._hash(name, "HASH_B")) in hashes:
                found.add(name)
        archive.file.close()
    missing = sorted(display for display, groups in candidates.items() if any(not (group & found) for group in groups))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(str(display) + "\n" for display in missing), encoding="utf-8")
    print(json.dumps({"displays": len(candidates), "paths": len(needed), "found": len(found),
                      "missing_displays": len(missing), "archives": len(archives)}))


if __name__ == "__main__":
    main()
