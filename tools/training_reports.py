"""Read-only aggregation and comparison of training reports."""
import argparse
import json
import math
from pathlib import Path
import statistics


def load(directory):
    reports = []
    for path in sorted(Path(directory).rglob("summary.json")):
        report = json.loads(path.read_text(encoding="utf-8"))
        if report.get("schemaVersion") != 1:
            raise ValueError(f"Unsupported schema in {path}")
        reports.append(report)
    if not reports:
        raise ValueError("No summary.json files found")
    return reports


def leaves(value, prefix=""):
    if isinstance(value, dict):
        for key, item in value.items():
            yield from leaves(item, f"{prefix}.{key}" if prefix else key)
    elif isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
        yield prefix, value


def aggregate(reports):
    result = {"schemaVersion": 1, "runs": len(reports), "metrics": {}}
    allowed = ("combat.", "healing.", "threat.", "resources.", "support.", "mechanics.")
    values = {}
    for report in reports:
        for key, value in leaves(report):
            if key.startswith(allowed):
                values.setdefault(key, []).append(value)
    for key, samples in sorted(values.items()):
        result["metrics"][key] = {
            "measuredRuns": len(samples), "mean": statistics.mean(samples),
            "median": statistics.median(samples), "min": min(samples), "max": max(samples),
            "stddev": statistics.pstdev(samples), "sum": sum(samples),
            "runsWithNonzeroValue": sum(x != 0 for x in samples),
        }
    return result


def identity(report):
    roster = report["normalization"]["roster"]
    return json.dumps({
        "schema": report["schemaVersion"], "class": report["classId"], "spec": report["specId"],
        "role": report["role"], "scenario": report["scenario"], "seed": report["seed"],
        "duration": report["requestedDurationMs"],
        "roster": [{**{k: member[k] for k in ("classId", "specId", "role", "gear")},
                    "level": member.get("level"), "spellbook": sorted(member.get("spellbook", [])),
                    "weaponSkills": member.get("weaponSkills")} for member in roster],
    }, sort_keys=True, separators=(",", ":"))


def compare(baseline, experiment):
    if sorted(map(identity, baseline)) != sorted(map(identity, experiment)):
        raise ValueError("Comparison requires matching scenarios, seeds, durations, roles, specs and party gear")
    left, right = aggregate(baseline), aggregate(experiment)
    result = {"schemaVersion": 1, "runs": len(baseline), "deltas": {}}
    for key in sorted(left["metrics"].keys() | right["metrics"].keys()):
        a, b = left["metrics"].get(key), right["metrics"].get(key)
        if not a or not b or a["measuredRuns"] != len(baseline) or b["measuredRuns"] != len(experiment):
            result["deltas"][key] = {"available": False}
            continue
        result["deltas"][key] = {
            "available": True, "baselineMean": a["mean"], "experimentMean": b["mean"],
            "absoluteDelta": b["mean"] - a["mean"],
            "percentDelta": 100 * (b["mean"] - a["mean"]) / abs(a["mean"]) if a["mean"] else None,
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    agg = commands.add_parser("aggregate")
    agg.add_argument("directory", type=Path)
    comp = commands.add_parser("compare")
    comp.add_argument("baseline", type=Path)
    comp.add_argument("experiment", type=Path)
    args = parser.parse_args()
    try:
        result = aggregate(load(args.directory)) if args.command == "aggregate" else compare(
            load(args.baseline), load(args.experiment))
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
