#!/usr/bin/env python3
"""
ISTR Processing Pipeline - Glue script to run the complete istr generation pipeline.

Usage:
    python istr_pipeline.py <build_dir> <output_dir> <sources_list> [additional_istrs]
"""

import os
import sys
import subprocess
from pathlib import Path


def ensure_directory(path):
    Path(path).mkdir(parents=True, exist_ok=True)


def read_sources_list(sources_file):
    sources = []
    try:
        with open(sources_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    sources.append(line)
    except IOError as e:
        print(f"Error reading sources list '{sources_file}': {e}")
        sys.exit(1)

    if not sources:
        print(f"No source files found in '{sources_file}'")
        sys.exit(1)

    return sources


def find_script(script_name):
    script_path = Path(__file__).parent / script_name
    if not script_path.exists():
        print(f"Required script '{script_name}' not found")
        sys.exit(1)
    return str(script_path)


def run_command(cmd, description):
    print(f"\n-- {description} --")
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if result.stdout.strip():
            print(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        print(f"Error during: {description}")
        if e.stdout:
            print("STDOUT:", e.stdout)
        if e.stderr:
            print("STDERR:", e.stderr)
        sys.exit(1)


def has_qcfg(content):
    return any(line.strip().startswith("QCFG(") for line in content.splitlines())


def main():
    if len(sys.argv) < 4 or len(sys.argv) > 5:
        print(__doc__)
        sys.exit(1)

    build_dir = sys.argv[1]
    output_dir = sys.argv[2]
    sources_list_file = sys.argv[3]
    additional_istrs_file = sys.argv[4] if len(sys.argv) == 5 else None

    ensure_directory(build_dir)
    ensure_directory(output_dir)

    makeistrdefs_script = find_script("makeistrdefs.py")
    makeistrdata_script = find_script("makeistrdata.py")

    sources = read_sources_list(sources_list_file)

    preprocessed_file = os.path.join(build_dir, "preprocessed.i")
    istr_split_dir = os.path.join(build_dir, "istr_split")
    combined_istrs_file = os.path.join(build_dir, "istrdefs_collected.h")
    final_output_file = os.path.join(output_dir, "istrdefs.generated.h")

    ensure_directory(istr_split_dir)

    preprocess_cmd = [
        "python", makeistrdefs_script, "pp", "pp", "gcc", "-E", "-dD", "-I.",
        "output", preprocessed_file,
        "cflags",
        "sources"
    ] + sources
    run_command(preprocess_cmd, "Preprocessing source files")

    split_cmd = [
        "python", makeistrdefs_script, "split", "istr",
        preprocessed_file, istr_split_dir
    ]
    run_command(split_cmd, "Extracting ISTR macros")

    cat_cmd = [
        "python", makeistrdefs_script, "cat", "istr",
        "unused", istr_split_dir, combined_istrs_file
    ]
    run_command(cat_cmd, "Combining ISTR files")

    #additional ISTRs and extract QCFG lines to place at the top
    config_lines = []
    additional_istrs = []

    if additional_istrs_file:
        try:
            with open(additional_istrs_file, 'r') as f:
                for line in f:
                    stripped = line.strip()
                    if stripped.startswith("QCFG("):
                        config_lines.append(stripped)
                    elif stripped:
                        additional_istrs.append(line.rstrip("\n"))
            print(f"Loaded additional ISTRs from '{additional_istrs_file}'")
        except IOError as e:
            print(f"Warning: Could not read additional ISTRs file '{additional_istrs_file}': {e}")

    default_config = [
        "QCFG(BYTES_IN_LEN, 1)",
        "QCFG(BYTES_IN_HASH, 2)"
    ]

    try:
        with open(combined_istrs_file, 'r') as f:
            combined_lines = f.read().splitlines()

        qcfg_in_combined = any(line.strip().startswith("QCFG(") for line in combined_lines)

        final_lines = []
        final_lines.append("# ISTR Configuration")

        #use passed-in QCFG lines, or fall back to default
        if config_lines:
            final_lines.extend(config_lines)
        elif not qcfg_in_combined:
            final_lines.extend(default_config)

        final_lines.append("")  # Spacer
        final_lines.extend(combined_lines)

        if additional_istrs:
            final_lines.append("")
            final_lines.append("# Additional ISTRs")
            final_lines.extend(additional_istrs)

        with open(combined_istrs_file, 'w') as f:
            f.write("\n".join(final_lines) + "\n")

    except IOError as e:
        print(f"Error writing combined ISTR file: {e}")
        sys.exit(1)

    try:
        result = subprocess.run(
            ["python", makeistrdata_script, combined_istrs_file],
            check=True, capture_output=True, text=True
        )
        with open(final_output_file, 'w') as f:
            f.write(result.stdout)
        print(f"Generated final header at {final_output_file}")
    except subprocess.CalledProcessError as e:
        print("Failed to generate final header")
        if e.stdout:
            print("STDOUT:", e.stdout)
        if e.stderr:
            print("STDERR:", e.stderr)
        sys.exit(1)

    try:
        with open(combined_istrs_file, 'r') as f:
            istr_lines = [line for line in f if line.strip().startswith('Q(')]

        with open(final_output_file, 'r') as f:
            qdef_lines = [line for line in f if line.strip().startswith('ISTRDEF(')]

        print("\n=== Summary ===")
        print(f"ISTRs processed: {len(istr_lines)}")
        print(f"ISTRDEFs generated: {len(qdef_lines)}")
    except IOError:
        pass

    print("All ISTR_* macros gleaned and prepared.")


if __name__ == "__main__":
    main()
