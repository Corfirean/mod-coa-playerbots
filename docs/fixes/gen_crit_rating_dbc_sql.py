# Extends gtchancetomeleecrit[base]_dbc, gtchancetospellcrit[base]_dbc, and
# gtoctclasscombatratingscalar_dbc (acore_world) with rows for Ascension's
# custom class ids 12-32, sourced from the on-disk .dbc files. Same root
# cause / same fix pattern as gt-regen-dbc-extend.sql -- see AGENTS.md.
# Without this:
#  - melee crit chance AND dodge chance from Agility (both read
#    sGtChanceToMeleeCritStore) are wrong for custom classes
#    (Player::GetMeleeCritFromAgility / GetDodgeFromAgility)
#  - spell crit chance from Intellect is wrong for custom classes
#    (Player::GetSpellCritFromIntellect)
#  - GetRatingMultiplier() silently falls back to 1.0 for every combat
#    rating (crit/haste/hit/dodge/parry/defense/resilience/expertise/armor
#    penetration rating) on every custom class, so gear Rating stats use a
#    flat, non-class/level-scaled conversion instead of the intended one
#
# Data-quality caveat (see AGENTS.md): the on-disk files are not a
# byte-accurate match for this server's live-tuned data -- cross-checking
# against still-correct stock rows found real mismatches beyond the
# class-10 gap. Applied anyway because the alternative (0 or a flat 1.0
# fallback) is strictly worse for custom classes, which had zero effective
# rows before this; treat the resulting numbers as "functional
# approximation," not "verified-correct tuning."
#
# gtOCTClassCombatRatingScalar.dbc has a different on-disk record layout
# than the other Gt tables here: 8 bytes/record (a real 4-byte id then a
# 4-byte float), not the 4-byte single-float-with-synthetic-index layout
# gtChanceToMeleeCrit/gtChanceToSpellCrit and their Base companions use.
# read_dbc_id_float_pairs() below handles that shape; the SQL table's ID
# column is 1-based to match the file's own embedded id field.

import struct
import sys

DBC_DIR = r"C:\games\CoA-Repack\Data\dbc"

# (dbc file, sql table, stock_row_count, total_expected_rows, shape)
# shape: "float" = single 4-byte float per record, positional 0-based index
#        "id_float" = real 4-byte id + 4-byte float per record, 1-based id
TABLES = [
    ("gtChanceToMeleeCrit.dbc", "gtchancetomeleecrit_dbc", 1100, 3200, "float"),
    ("gtChanceToMeleeCritBase.dbc", "gtchancetomeleecritbase_dbc", 11, 32, "float"),
    ("gtChanceToSpellCrit.dbc", "gtchancetospellcrit_dbc", 1100, 3200, "float"),
    ("gtChanceToSpellCritBase.dbc", "gtchancetospellcritbase_dbc", 11, 32, "float"),
    ("gtOCTClassCombatRatingScalar.dbc", "gtoctclasscombatratingscalar_dbc", 352, 1024, "id_float"),
]


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


def read_dbc_id_float_pairs(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = data[0:4]
    if magic != b"WDBC":
        raise RuntimeError(f"{path}: not a WDBC file (magic={magic!r})")
    record_count, field_count, record_size, string_block_size = struct.unpack_from("<IIII", data, 4)
    if field_count != 2 or record_size != 8:
        raise RuntimeError(f"{path}: unexpected shape fields={field_count} recordSize={record_size}")
    header_size = 20
    pairs = {}
    for i in range(record_count):
        rec_id, val = struct.unpack_from("<If", data, header_size + i * 8)
        pairs[rec_id] = val
    return pairs


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "crit_rating_dbc_fix.sql"
    lines = []
    lines.append("-- See the header comment in gen_crit_rating_dbc_sql.py for full context.")
    lines.append("")

    for dbc_file, table, stock_rows, total_expected, shape in TABLES:
        path = f"{DBC_DIR}\\{dbc_file}"
        rows = []

        if shape == "float":
            floats = read_dbc_floats(path)
            if len(floats) < total_expected:
                print(f"WARNING: {dbc_file} has only {len(floats)} records, expected >= {total_expected}", file=sys.stderr)
            for idx in range(stock_rows, min(len(floats), total_expected)):
                rows.append((idx, floats[idx]))
            source_count = len(floats)
        else:  # id_float, 1-based ids
            pairs = read_dbc_id_float_pairs(path)
            source_count = len(pairs)
            for rec_id in range(stock_rows + 1, total_expected + 1):
                if rec_id not in pairs:
                    print(f"WARNING: {dbc_file} missing id {rec_id}", file=sys.stderr)
                    continue
                rows.append((rec_id, pairs[rec_id]))

        lines.append(f"-- {table} (from {dbc_file}, {source_count} records on disk)")
        row_strs = [f"({rid}, {val!r})" for rid, val in rows]
        for chunk_start in range(0, len(row_strs), 200):
            chunk = row_strs[chunk_start:chunk_start + 200]
            lines.append(f"REPLACE INTO {table} (ID, Data) VALUES\n" + ",\n".join(chunk) + ";")
        lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
