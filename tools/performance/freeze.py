import argparse
from pathlib import Path
import shutil
import subprocess
import zipfile

from benchmark import digest, read_json, source_identity, write_json


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module-source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--core-source', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--verification', type=Path, required=True)
    parser.add_argument('--build-cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--symbols', action='store_true')
    args = parser.parse_args()
    report = read_json(args.verification)
    if any(report['stages'].get(stage, {}).get('status') != 'passed' for stage in ['source', 'build', 'unit']):
        raise RuntimeError('Source/build/unit verification must pass before freezing')
    build = Path(report['stages']['build']['summary']['build_directory']).resolve()
    if not args.binary.resolve().is_relative_to(build):
        raise RuntimeError('Binary does not belong to the verified build directory')
    identity = source_identity(args.module_source)
    materialized = source_identity(args.core_source / 'modules/mod-coa-playerbots')
    if identity['source_files_sha256'] != materialized['source_files_sha256']:
        raise RuntimeError('Materialized module differs from authoritative source')
    settings = {}
    for line in args.build_cache.read_text(encoding='utf-8-sig').splitlines():
        if ':' not in line or '=' not in line or line.startswith(('#', '//')):
            continue
        name, value = line.split('=', 1)
        name = name.split(':', 1)[0]
        if '-ADVANCED' not in name and (name in ['CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'SCRIPTS', 'MODULES', 'BUILD_TESTING'] or
                                      name.startswith(('CMAKE_CXX_FLAGS', 'WITH_'))):
            settings[name] = value
    args.output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.binary, args.output / 'worldserver.exe')
    if args.symbols:
        shutil.copy2(args.binary.with_suffix('.pdb'), args.output / 'worldserver.pdb')
    with zipfile.ZipFile(args.output / 'module-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted((args.module_source / 'module/src').rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(args.module_source).as_posix())
    patch = subprocess.run(['git', '-C', str(args.core_source), 'diff', 'HEAD', '--', 'src'], capture_output=True, check=True)
    (args.output / 'core-changes.patch').write_bytes(patch.stdout)
    files = subprocess.run(['git', '-C', str(args.core_source), 'ls-files', '--others', '--exclude-standard', '-z', '--', 'src'],
                           capture_output=True, check=True).stdout.decode('utf-8').split('\0')
    with zipfile.ZipFile(args.output / 'core-new-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in files:
            if name:
                archive.write(args.core_source / name, name)
    write_json(args.output / 'build-manifest.json', dict(binary_sha256=digest(args.output / 'worldserver.exe'),
               module=identity, core=source_identity(args.core_source), cmake_settings=settings,
               verification=str(args.verification.resolve()), freeze_script_sha256=digest(Path(__file__)),
               source_archives={name: digest(args.output / name) for name in ['module-source.zip', 'core-changes.patch', 'core-new-source.zip']},
               symbols_sha256=digest(args.output / 'worldserver.pdb') if args.symbols else None))
    print('Frozen:', args.output)


if __name__ == '__main__':
    main()
