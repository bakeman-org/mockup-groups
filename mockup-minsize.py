#!/usr/bin/env python3

import subprocess
import tempfile
import argparse
import shutil
import os
import re
import base64

MARKER = "# __GEN_ELF_BUNDLE_MARKER__"

ap = argparse.ArgumentParser(
    description='Easily mock up your C/C++ programs into platform-independent, self-contained executable files. No more GLIBC version pains.'
)
ap.add_argument('filename', help='path to executable ELF file', nargs='+')
ap.add_argument('-o', '--output', help='output directory to copy ELF files', default='')
ap.add_argument('-f', '--force', help='force clean directory if exists', action='store_true')
ap.add_argument('-D', '--dry', help='dry run mode, print dependencies only', action='store_true')
ap.add_argument('-P', '--patch', help='patch ELF files RPATH (requires patchelf)', action='store_true')
ap.add_argument('-S', '--single', help='output a single self‑extract file instead of directory (NO base64)', action='store_true')
ap.add_argument('-x', '--suffix', help='start up script file suffix (default .sh)', default='.sh')
args = ap.parse_args()

if args.single:
    temp_dir_object = tempfile.TemporaryDirectory()
    output_dir = temp_dir_object.name
else:
    if args.output:
        output_dir = args.output
    else:
        output_dir = 'bin'

if not args.dry:
    try:
        os.makedirs(output_dir, exist_ok=True)
    except FileExistsError:
        if args.force:
            shutil.rmtree(output_dir)
            os.makedirs(output_dir)

files = [os.path.abspath(p) for p in args.filename]
depends = {os.path.basename(p): p for p in files}
updates = list(files)

print('Mocking:', files)

while updates:
    lines = subprocess.check_output(
        ['ldd', *updates],
        env={'LC_ALL': 'C', 'LANG': 'C', 'LANGUAGE': 'C'}
    ).decode().splitlines()
    updates = []
    for line in lines:
        if not line.startswith('\t'):
            continue
        if '=>' in line:
            m = re.findall(r'^\t(\S+) => (\S+)(?:\s\([0-9a-fx]+\))?$', line)
            if not m:
                continue
            name, path = m[0]
            name = os.path.basename(name)
        else:
            m = re.findall(r'^\t(/\S+ld-linux[^\s]*)\s*\(0x[0-9a-f]+\)$', line)
            if not m:
                continue
            path = m[0]
            name = os.path.basename(path)
        if name in depends:
            if depends[name] != path:
                print(f'WARNING: multiple path found for {name}: {depends[name]}, {path}')
            continue
        if path == 'not found':
            print(f'WARNING: dependency not found for {name}')
            continue
        depends[name] = path
        print('Found:', name, '=>', path)
        updates.append(path)

print('Dependencies collected, total:', len(depends))

ld_linux = None
for n in list(depends.keys()):
    if n.startswith('ld-linux-'):
        ld_linux = n
        path_ld = depends.pop(n)
        dest = os.path.join(output_dir, n)
        print('Copying ld-linux:', path_ld, '=>', dest)
        if not args.dry:
            shutil.copyfile(path_ld, dest)
            os.chmod(dest, 0o755)
        break

for name, path in depends.items():
    dest = os.path.join(output_dir, name)
    print('Copying:', path, '=>', dest)
    if not args.dry:
        shutil.copyfile(path, dest)
    if args.patch and not args.dry:
        os.chmod(dest, 0o755)
        subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN', dest])
        if name.endswith('.so') or '.so.' in name:
            subprocess.check_call(['patchelf', '--set-soname', name, dest])
    if path in files and ld_linux and not args.dry:
        subprocess.check_call(['patchelf', '--set-interpreter', f'./{ld_linux}', dest])

if not args.single and args.suffix:
    for path in args.filename:
        name = os.path.basename(path)
        script = os.path.join(output_dir, name + args.suffix)
        print('Creating startup script:', script)
        if not args.dry:
            with open(script, 'w') as f:
                f.write('#!/bin/bash\nset -e\n')
                f.write(f'test -x "$(dirname "$0")/{ld_linux}" || chmod +x "$(dirname "$0")/{ld_linux}"\n')
                f.write(f'test -x "$(dirname "$0")/{name}" || chmod +x "$(dirname "$0")/{name}"\n')
                if not args.patch:
                    f.write(f'LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$(dirname "$0")" exec -a "$0" "$(dirname "$0")/{ld_linux}" "$(dirname "$0")/{name}" "$@"\n')
                else:
                    f.write(f'exec -a "$0" "$(dirname "$0")/{ld_linux}" "$(dirname "$0")/{name}" "$@"\n')
            os.chmod(script, 0o755)

if args.single and not args.dry:
    for path in args.filename:
        exe_name = os.path.basename(path)
        if args.output:
            out_script = args.output
        else:
            out_script = exe_name + '.bundle'

        old_cwd = os.getcwd()
        os.chdir(output_dir)
        tar_proc = subprocess.Popen(
            ['tar', '-cf', '-', '.'],
            stdout=subprocess.PIPE
        )
        xz_proc = subprocess.Popen(
            ['xz', '-6', '--stdout'],
            stdin=tar_proc.stdout,
            stdout=subprocess.PIPE
        )
        tar_proc.stdout.close()
        raw_tar_xz, _ = xz_proc.communicate()
        tar_rc = tar_proc.wait()
        xz_rc = xz_proc.wait()
        os.chdir(old_cwd)
        if tar_rc !=0 or xz_rc !=0:
            raise RuntimeError(f"tar/xz failed tar_rc={tar_rc} xz_rc={xz_rc}")

        b64_data = base64.b64encode(raw_tar_xz).decode('ascii')

        shell_header = f'''#!/bin/bash
set -e
cleanup() {{ rm -rf "$_extractdir"; }}
_extractdir=$(mktemp -d)
trap cleanup EXIT INT TERM

base64 -d <<'BUNDLE_EOF' | xz -d - | tar -xf - -C "$_extractdir"
{b64_data}
BUNDLE_EOF

chmod +x "$_extractdir/{ld_linux}" "$_extractdir/{exe_name}"
export LD_LIBRARY_PATH="$_extractdir:$LD_LIBRARY_PATH"
exec -a "$0" "$_extractdir/{ld_linux}" "$_extractdir/{exe_name}" "$@"
'''
        with open(out_script, 'w', encoding='utf-8') as fout:
            fout.write(shell_header)
        os.chmod(out_script, 0o755)
        print(f'Generated single bundle: {out_script}, raw_xz bytes={len(raw_tar_xz)}')
        final_output = out_script
        print()
        print(f'Done! Run ./{final_output}')
else:
    if not args.dry:
        sample_run = os.path.join(output_dir, os.path.basename(files[0]) + args.suffix)
        print()
        print(f'Done! Run {sample_run}')
