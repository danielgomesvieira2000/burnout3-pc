#!/usr/bin/env python3
"""Print translated functions by address, from the build's generated sources.

    python games/burnout3/tools/show_functions.py 0022EB30 0022EB20 > functions.txt

Looks through every .cpp under games/burnout3/build (or --build DIR) for the
definition of F_<address> and prints it whole. The sources are made from the
disc on the player's PC; this shows a few functions, to read or to share.
"""

import argparse
import os
import re
import sys


def definitions(path, wanted):
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return
    for name in wanted:
        for match in re.finditer(r"\bvoid\s+%s\s*\([^;{)]*\)\s*\{" % re.escape(name), text):
            depth, end = 0, match.end() - 1
            while end < len(text):
                if text[end] == "{":
                    depth += 1
                elif text[end] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                end += 1
            start = text.rfind("\n", 0, match.start()) + 1
            yield name, path, text[start:end + 1]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("addresses", nargs="+", help="addresses in hex, e.g. 0022EB30")
    parser.add_argument("--build", default=os.path.join(here, "..", "build"))
    args = parser.parse_args()
    wanted = ["F_%08X" % int(a, 16) for a in args.addresses]
    found = set()
    for root, _, files in os.walk(args.build):
        for file in files:
            if file.endswith(".cpp"):
                for name, path, body in definitions(os.path.join(root, file), wanted):
                    if name in found:
                        continue
                    found.add(name)
                    print("// %s, from %s\n%s\n" % (name, os.path.relpath(path, args.build), body))
    for name in wanted:
        if name not in found:
            print("// %s: not found" % name)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
