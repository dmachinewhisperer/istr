#!/usr/bin/env python3
"""
QSTR Processing Pipeline - Glue script to run the complete qstr generation pipeline.

This script orchestrates the entire process:
1. Preprocesses source files to extract MP_QSTR_* macros
2. Combines all extracted qstrs with optional additional qstrs
3. Generates the final qstr header file with hash/length data

Usage:
    python qstr_pipeline.py <build_dir> <output_dir> <sources_list> [additional_qstrs]

Arguments:
    build_dir       - Directory for intermediate files (will be created if not exists)
    output_dir      - Directory for final output header file
    sources_list    - Text file containing list of source files (one per line)
    additional_qstrs - Optional file with additional Q(...) definitions
"""

import os
import sys
import subprocess
import tempfile
import shutil
from pathlib import Path


def ensure_directory(path):
    """Create directory if it doesn't exist."""
    Path(path).mkdir(parents=True, exist_ok=True)


def read_sources_list(sources_file):
    """Read list of source files from text file."""
    sources = []
    try:
        with open(sources_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):  # Skip empty lines and comments
                    sources.append(line)
    except IOError as e:
        print(f"Error reading sources list file '{sources_file}': {e}")
        sys.exit(1)
    
    if not sources:
        print(f"No source files found in '{sources_file}'")
        sys.exit(1)
    
    return sources


def find_script(script_name):
    """Find script in the same directory as this pipeline script."""
    script_dir = Path(__file__).parent
    script_path = script_dir / script_name
    if not script_path.exists():
        print(f"Error: Required script '{script_name}' not found in {script_dir}")
        sys.exit(1)
    return str(script_path)


def run_command(cmd, description):
    """Run a command and handle errors."""
    print(f"Running: {description}")
    print(f"Command: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if result.stdout:
            print("Output:", result.stdout.strip())
        return result
    except subprocess.CalledProcessError as e:
        print(f"Error running {description}:")
        print(f"Command failed with return code {e.returncode}")
        if e.stdout:
            print("STDOUT:", e.stdout)
        if e.stderr:
            print("STDERR:", e.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"Error: Could not find required program for {description}")
        sys.exit(1)


def main():
    if len(sys.argv) < 4 or len(sys.argv) > 5:
        print(__doc__)
        sys.exit(1)
    
    build_dir = sys.argv[1]
    output_dir = sys.argv[2]
    sources_list_file = sys.argv[3]
    additional_qstrs_file = sys.argv[4] if len(sys.argv) == 5 else None
    
    # Ensure directories exist
    ensure_directory(build_dir)
    ensure_directory(output_dir)
    
    # Find required scripts
    makeqstrdefs_script = find_script("makeqstrdefs.py")
    makeqstrdata_script = find_script("makeqstrdata.py")
    
    # Read source files list
    sources = read_sources_list(sources_list_file)
    print(f"Processing {len(sources)} source files")
    
    # Define intermediate file paths
    preprocessed_file = os.path.join(build_dir, "preprocessed.i")
    qstr_split_dir = os.path.join(build_dir, "qstr_split")
    combined_qstrs_file = os.path.join(build_dir, "qstrdefs_collected.h")
    final_output_file = os.path.join(output_dir, "qstrdefs_generated.h")
    
    ensure_directory(qstr_split_dir)
    
    # Step 1: Preprocess all source files
    print("\n=== Step 1: Preprocessing source files ===")
    preprocess_cmd = [
        "python", makeqstrdefs_script, "pp",
        "pp", "gcc", "-E", "-dD", "-I.", 
        "output", preprocessed_file,
        "cflags", "-DMICROPYTHON_QSTR_EXTRA_POOL=mp_qstr_frozen_const_pool",
        "sources"
    ] + sources
    
    run_command(preprocess_cmd, "C preprocessor on source files")
    
    # Step 2: Split preprocessed output to extract qstrs
    print("\n=== Step 2: Extracting QSTR definitions ===")
    split_cmd = [
        "python", makeqstrdefs_script, "split", "qstr", 
        preprocessed_file, qstr_split_dir
    ]
    
    run_command(split_cmd, "QSTR extraction from preprocessed files")
    
    # Step 3: Combine all qstr files
    print("\n=== Step 3: Combining QSTR definitions ===")
    cat_cmd = [
        "python", makeqstrdefs_script, "cat", "qstr",
        "unused", qstr_split_dir, combined_qstrs_file
    ]
    
    run_command(cat_cmd, "Combining QSTR files")
    
    # Step 4: Add additional qstrs if provided
    if additional_qstrs_file:
        print(f"\n=== Step 4: Adding additional QSTRs from {additional_qstrs_file} ===")
        try:
            with open(additional_qstrs_file, 'r') as additional_f:
                additional_content = additional_f.read()
            
            with open(combined_qstrs_file, 'a') as combined_f:
                combined_f.write("\n# Additional QSTRs\n")
                combined_f.write(additional_content)
            
            print(f"Added additional QSTRs from '{additional_qstrs_file}'")
        except IOError as e:
            print(f"Warning: Could not read additional qstrs file '{additional_qstrs_file}': {e}")
    
    # Step 5: Add required configuration
    print("\n=== Step 5: Adding configuration ===")
    config_content = """
# QSTR Configuration
QCFG(BYTES_IN_LEN, 1)
QCFG(BYTES_IN_HASH, 2)
"""
    
    try:
        with open(combined_qstrs_file, 'r') as f:
            existing_content = f.read()
        
        with open(combined_qstrs_file, 'w') as f:
            f.write(config_content)
            f.write(existing_content)
    except IOError as e:
        print(f"Error adding configuration to combined file: {e}")
        sys.exit(1)
    
    # Step 6: Generate final qstr data header
    print("\n=== Step 6: Generating final QSTR header ===")
    
    # Run makeqstrdata and capture output
    makeqstrdata_cmd = ["python", makeqstrdata_script, combined_qstrs_file]
    
    try:
        result = subprocess.run(makeqstrdata_cmd, check=True, capture_output=True, text=True)
        
        # Write output to final header file
        with open(final_output_file, 'w') as f:
            f.write(result.stdout)
        
        print(f"Generated final QSTR header: {final_output_file}")
        
    except subprocess.CalledProcessError as e:
        print("Error generating final QSTR header:")
        print(f"Command failed with return code {e.returncode}")
        if e.stdout:
            print("STDOUT:", e.stdout)
        if e.stderr:
            print("STDERR:", e.stderr)
        sys.exit(1)
    
    # Step 7: Summary
    print("\n=== Pipeline Complete ===")
    print(f"Build directory: {build_dir}/")
    print(f"  - Preprocessed file: {os.path.basename(preprocessed_file)}")
    print(f"  - Split QSTRs directory: {os.path.basename(qstr_split_dir)}/")
    print(f"  - Combined QSTRs: {os.path.basename(combined_qstrs_file)}")
    print(f"Final output: {final_output_file}")
    
    # Show some statistics
    try:
        with open(combined_qstrs_file, 'r') as f:
            lines = f.readlines()
        
        qstr_lines = [line for line in lines if line.strip().startswith('Q(')]
        print(f"Total QSTR definitions processed: {len(qstr_lines)}")
        
        with open(final_output_file, 'r') as f:
            final_lines = f.readlines()
        
        qdef_lines = [line for line in final_lines if line.strip().startswith('QDEF(')]
        print(f"Total QDEF entries generated: {len(qdef_lines)}")
        
    except IOError:
        pass  # Statistics are optional
    
    print("\nPipeline completed successfully!")


if __name__ == "__main__":
    main()