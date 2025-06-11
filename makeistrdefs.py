"""
This script processes the output from the C preprocessor and extracts
ISTR_* macros and ISTR_COMPRESSED_ROM_TEXT macros.

This script works with Python 2.6, 2.7, 3.3 and 3.4.
"""

from __future__ import print_function

import io
import os
import re
import subprocess
import sys
import multiprocessing, multiprocessing.dummy


#ISTR_FOO macros.
_MODE_ISTR = "istr"

# ISTR_COMPRESSED_ROM_TEXT("") macros. 
_MODE_COMPRESS = "compress"


class PreprocessorError(Exception):
    pass


def is_c_source(fname):
    return os.path.splitext(fname)[1] in [".c"]


def is_cxx_source(fname):
    return os.path.splitext(fname)[1] in [".cc", ".cp", ".cxx", ".cpp", ".CPP", ".c++", ".C"]


def preprocess():
    if any(src in args.dependencies for src in args.changed_sources):
        sources = args.sources
    elif any(args.changed_sources):
        sources = args.changed_sources
    else:
        sources = args.sources
    
    csources = []
    cxxsources = []
    for source in sources:
        if is_cxx_source(source):
            cxxsources.append(source)
        elif is_c_source(source):
            csources.append(source)
    
    try:
        os.makedirs(os.path.dirname(args.output[0]))
    except OSError:
        pass

    def pp(flags):
        def run(files):
            try:
                return subprocess.check_output(args.pp + flags + files)
            except subprocess.CalledProcessError as er:
                raise PreprocessorError(str(er))
        return run

    try:
        cpus = multiprocessing.cpu_count()
    except NotImplementedError:
        cpus = 1
    
    p = multiprocessing.dummy.Pool(cpus)
    with open(args.output[0], "wb") as out_file:
        for flags, sources in (
            (args.cflags, csources),
            (args.cxxflags, cxxsources),
        ):
            if not sources:
                continue
            batch_size = (len(sources) + cpus - 1) // cpus
            chunks = [sources[i : i + batch_size] for i in range(0, len(sources), batch_size or 1)]
            for output in p.imap(pp(flags), chunks):
                out_file.write(output)


def write_out(fname, output):
    if output and fname:
        # Sanitize filename for filesystem
        for m, r in [("/", "__"), ("\\", "__"), (":", "@"), ("..", "@@")]:
            fname = fname.replace(m, r)
        with open(args.output_dir + "/" + fname + "." + args.mode, "w") as f:
            f.write("\n".join(output) + "\n")


def process_file(f):
    # Match gcc-like output (# n "file") and msvc-like output (#line n "file")
    re_line = re.compile(r"^#(?:line)?\s+\d+\s\"([^\"]+)\"")
    
    if args.mode == _MODE_ISTR:
        re_match = re.compile(r"ISTR_[_a-zA-Z0-9]+")
    elif args.mode == _MODE_COMPRESS:
        re_match = re.compile(r'ISTR_COMPRESSED_ROM_TEXT\("([^"]*)"\)')
    else:
        raise ValueError("Invalid mode: {}".format(args.mode))
    
    output = []
    last_fname = None
    
    for line in f:
        if line.isspace():
            continue
        
        # Check for file location markers
        m = re_line.match(line)
        if m:
            fname = m.group(1)
            if not is_c_source(fname) and not is_cxx_source(fname):
                continue
            if fname != last_fname:
                write_out(last_fname, output)
                output = []
                last_fname = fname
            continue
        
        # Extract matches based on mode
        for match in re_match.findall(line):
            if args.mode == _MODE_ISTR:
                name = match.replace("ISTR_", "")
                output.append("Q(" + name + ")")
            elif args.mode == _MODE_COMPRESS:
                output.append(match)

    # Write out the final file
    if last_fname:
        write_out(last_fname, output)
    
    return ""


def cat_together():
    import glob
    import hashlib

    hasher = hashlib.md5()
    all_lines = []
    
    # Collect all processed files
    for fname in glob.glob(args.output_dir + "/*." + args.mode):
        with open(fname, "rb") as f:
            lines = f.readlines()
            all_lines += lines
    
    all_lines.sort()
    all_lines = b"\n".join(all_lines)
    hasher.update(all_lines)
    new_hash = hasher.hexdigest()
    
    # Check if output has changed
    old_hash = None
    try:
        with open(args.output_file + ".hash") as f:
            old_hash = f.read()
    except IOError:
        pass
    
    mode_full = "ISTR" if args.mode == _MODE_ISTR else "Compressed data"
    
    if old_hash != new_hash or not os.path.exists(args.output_file):
        print(mode_full, "updated")
        with open(args.output_file, "wb") as outf:
            outf.write(all_lines)
        with open(args.output_file + ".hash", "w") as f:
            f.write(new_hash)
    else:
        print(mode_full, "not updated")


if __name__ == "__main__":
    # Handle preprocessing command first
    if len(sys.argv) > 1 and sys.argv[1] == "pp":
        class Args:
            pass

        args = Args()
        args.command = sys.argv[1]

        named_args = {
            s: []
            for s in [
                "pp",
                "output",
                "cflags",
                "cxxflags", 
                "sources",
                "changed_sources",
                "dependencies",
            ]
        }

        current_tok = None
        for arg in sys.argv[1:]:
            if arg in named_args:
                current_tok = arg
            elif current_tok:
                named_args[current_tok].append(arg)

        if not named_args["pp"] or len(named_args["output"]) != 1:
            print("usage: %s pp pp <preprocessor> output <output_file> cflags <flags> sources <sources>" % sys.argv[0])
            sys.exit(2)

        for k, v in named_args.items():
            setattr(args, k, v)

        try:
            preprocess()
        except PreprocessorError as er:
            print(er)
            sys.exit(1)

        sys.exit(0)

    # Handle other commands - adjust argument count check
    min_args = 5  # command, mode, input_filename, output_dir
    if len(sys.argv) < min_args:
        print("usage: %s command mode input_filename output_dir [output_file]" % sys.argv[0])
        print("commands: pp, split, cat")
        print("modes: istr, compress")
        sys.exit(2)

    class Args:
        pass

    args = Args()
    args.command = sys.argv[1]
    args.mode = sys.argv[2]
    args.input_filename = sys.argv[3]  # Unused for command=cat
    args.output_dir = sys.argv[4]
    args.output_file = None if len(sys.argv) <= 5 else sys.argv[5]  # Optional for split, required for cat

    if args.mode not in (_MODE_ISTR, _MODE_COMPRESS):
        print("error: mode %s unrecognised. Valid modes: istr, compress" % args.mode)
        sys.exit(2)

    try:
        os.makedirs(args.output_dir)
    except OSError:
        pass

    if args.command == "split":
        with io.open(args.input_filename, encoding="utf-8") as infile:
            process_file(infile)

    elif args.command == "cat":
        if not args.output_file:
            print("error: output_file required for cat command")
            sys.exit(2)
        cat_together()
    
    else:
        print("error: unknown command %s. Valid commands: pp, split, cat" % args.command)
        sys.exit(2)