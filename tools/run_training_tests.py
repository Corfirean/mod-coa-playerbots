import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--module", type=Path, default=Path(__file__).resolve().parents[1] / "module")
    module = parser.parse_args().module
    compiler = shutil.which("cl")
    if not compiler:
        raise SystemExit("MSVC developer environment is required")
    with tempfile.TemporaryDirectory(prefix="coa-training-") as directory:
        executable = Path(directory) / "training_tests.exe"
        subprocess.run([compiler, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/O2",
                        "/I" + str(module / "src"), str(module / "tests/training_data_tests.cpp"),
                        str(module / "src/training/TrainingScenario.cpp"), "/Fe:" + str(executable)],
                       cwd=directory, check=True)
        subprocess.run([str(executable)], check=True)
        resolver = Path(directory) / "spell_resolver_tests.exe"
        subprocess.run([compiler, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/O2",
                        "/I" + str(module / "tests/resolver_stub"),
                        "/I" + str(module / "tests/stub"), "/I" + str(module / "src"),
                        str(module / "tests/spell_resolver_tests.cpp"),
                        str(module / "src/engine/SpellResolver.cpp"), "/Fe:" + str(resolver)],
                       cwd=directory, check=True)
        subprocess.run([str(resolver)], check=True)
    subprocess.run([sys.executable, "-B", str(module / "tests/test_training_reports.py")], check=True)


if __name__ == "__main__":
    main()
