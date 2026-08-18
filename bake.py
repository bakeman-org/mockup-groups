#!/usr/bin/env python3
import argparse
import tarfile
import io
import os
import time

SHELL_RUNTIME = r'''#!/bin/sh
MARKER="# __PY_BAKE_BUNDLE__"

has_bundled_data() {
    local line
    line=$(awk -v m="$MARKER" '$0 == m {print NR; exit}' "$0")
    [ -n "$line" ] && [ $(wc -l < "$0") -gt "$line" ]
}

main() {
    if ! has_bundled_data; then
        echo "error: embedded bundle payload missing" >&2
        exit 1
    fi

    extract_dir=$(mktemp -d)
    trap 'rm -rf "$extract_dir"' EXIT INT TERM

    marker_line=$(awk -v m="$MARKER" '$0 == m {print NR+1; exit}' "$0")
    tail -n +"$marker_line" "$0" | sed 's/^#//' | tar -xzf - -C "$extract_dir"

    cd "$extract_dir" || exit 1
    chmod +x ./__exe__
    exec ./__exe__ "$@"
}

main "$@"
# __PY_BAKE_BUNDLE__
'''

def main():
    parser = argparse.ArgumentParser(description="bake executable + resources into single .bake self‑extract shell")
    parser.add_argument("-e", "--executable", required=True, help="path to target executable binary")
    parser.add_argument("-r", "--resource", action="append", default=[], help="resource file/folder, multiple allowed")
    parser.add_argument("-o", "--output", help="output file, optional; default: <executable>.bake")
    args = parser.parse_args()

    if not args.output:
        args.output = f"{args.executable}.bake"

    tar_buffer = io.BytesIO()
    fixed_mtime = 1750000000

    with tarfile.open(fileobj=tar_buffer, mode="w:gz") as tar:
        # add executable as __exe__, override mtime
        ti = tar.gettarinfo(args.executable, arcname="__exe__")
        ti.mtime = fixed_mtime
        with open(args.executable, "rb") as f:
            tar.addfile(ti, f)

        for res in args.resource:
            for root, _, files in os.walk(res):
                for name in files:
                    full_path = os.path.join(root, name)
                    arc = os.path.relpath(full_path, os.getcwd())
                    ti = tar.gettarinfo(full_path, arcname=arc)
                    ti.mtime = fixed_mtime
                    with open(full_path, "rb") as f:
                        tar.addfile(ti, f)
            if os.path.isdir(res):
                arc_dir = os.path.relpath(res, os.getcwd())
                ti_dir = tar.gettarinfo(res, arcname=arc_dir)
                ti_dir.mtime = fixed_mtime
                tar.addfile(ti_dir)

    tar_buffer.seek(0)
    raw_tar = tar_buffer.read()

    with open(args.output, "wb") as fout:
        fout.write(SHELL_RUNTIME.encode("utf-8"))
        stream = io.BytesIO(raw_tar)
        for line in stream:
            fout.write(b"#" + line)

    os.chmod(args.output, 0o755)
    print(f"Generated: {args.output}")

if __name__ == "__main__":
    main()
