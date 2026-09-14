import struct
import sys

DBC_DIR = r"C:\games\CoA-Repack\Data\dbc"

TABLES = [
    ("gtOCTRegenHP.dbc", "gtoctregenhp_dbc"),
    ("gtRegenHPPerSpt.dbc", "gtregenhpperspt_dbc"),
    ("gtRegenMPPerSpt.dbc", "gtregenmpperspt_dbc"),
]

STOCK_ROWS = 1100  # 11 classes * 100 levels -- what the SQL hotfix tables currently have
GT_MAX_LEVEL = 100
MAX_CUSTOM_CLASS = 32  # Ascension's highest custom class id


def read_dbc_floats(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = data[0:4]
    if magic != b"WDBC":
        raise RuntimeError(f"{path}: not a WDBC file (magic={magic!r})")
    record_count, field_count, record_size, string_block_size = struct.unpack_from("<IIII", data, 4)
    if field_count != 1 or record_size != 4:
        raise RuntimeError(f"{path}: unexpected shape fields={field_count} recordSize={record_size}")
    header_size = 20
    floats = []
    for i in range(record_count):
        (val,) = struct.unpack_from("<f", data, header_size + i * 4)
        floats.append(val)
    return floats


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "regen_dbc_fix.sql"
    lines = []
    lines.append("-- Extends the gt*regen*_dbc hotfix tables (acore_world) with rows for")
    lines.append("-- Ascension's custom class ids 12-32, sourced from the already-extended")
    lines.append("-- on-disk .dbc files. Without this, DBCStorageBase::LoadFromDB() replaces")
    lines.append("-- the file-loaded 3200-row table with the DB table's 1100 rows, so any")
    lines.append("-- OCTRegenHPPerSpirit()/OCTRegenMPPerSpirit() lookup for a custom class")
    lines.append("-- indexes past row 1099 and returns null -> 0 regen.")
    lines.append("")

    for dbc_file, table in TABLES:
        path = f"{DBC_DIR}\\{dbc_file}"
        floats = read_dbc_floats(path)
        total_expected = MAX_CUSTOM_CLASS * GT_MAX_LEVEL
        if len(floats) < total_expected:
            print(f"WARNING: {dbc_file} has only {len(floats)} records, expected >= {total_expected}", file=sys.stderr)

        lines.append(f"-- {table} (from {dbc_file}, {len(floats)} records on disk)")
        rows = []
        for idx in range(STOCK_ROWS, min(len(floats), total_expected)):
            val = floats[idx]
            rows.append(f"({idx}, {val!r})")
        for chunk_start in range(0, len(rows), 200):
            chunk = rows[chunk_start:chunk_start + 200]
            lines.append(f"REPLACE INTO {table} (ID, Data) VALUES\n" + ",\n".join(chunk) + ";")
        lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
