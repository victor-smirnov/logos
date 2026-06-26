#!/usr/bin/env python3
import json
import argparse
import os
import re

def check_coverage(spec_json_path, source_dir):
    with open(spec_json_path, 'r') as f:
        spec = json.load(f)
        
    req_ids = []
    if 'invariants' in spec:
        req_ids = [inv['id'] for inv in spec['invariants'] if 'id' in inv]
    
    if not req_ids:
        print("No requirements found in spec.")
        return True
        
    found_ids = set()
    for root, _, files in os.walk(source_dir):
        for file in files:
            if not file.endswith(('.cpp', '.hpp', '.h', '.c')):
                continue
            path = os.path.join(root, file)
            with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                for req in req_ids:
                    if req in content:
                        found_ids.add(req)
                        
    missing = set(req_ids) - found_ids
    if missing:
        print("MISSING COVERAGE:")
        for m in sorted(missing):
            print(f"  {m}")
        return False
    else:
        print("Full assertion coverage achieved.")
        return True

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("spec", help="Path to JSON spec (e.g. writ-abi.json)")
    parser.add_argument("src", help="Source directory to check")
    args = parser.parse_args()
    
    success = check_coverage(args.spec, args.src)
    import sys
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
