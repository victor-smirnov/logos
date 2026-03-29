#!/usr/bin/env python3
import sqlite3
import argparse
import sys
import json

def summarize_level_0(db_path):
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    
    print("Level 0: Overview")
    print("=================")
    
    c.execute("SELECT requirement_id, COUNT(*) FROM assertions GROUP BY requirement_id")
    asserts = c.fetchall()
    
    total_asserts = sum(count for _, count in asserts)
    print(f"Assertion failures: {total_asserts}")
    for req_id, count in asserts:
        print(f"  {req_id}: {count} occurrences")
        
    c.execute("SELECT COUNT(*) FROM traces")
    total_traces = c.fetchone()[0]
    print(f"\nTotal traces: {total_traces}")
    
    c.execute("""
        SELECT 
            SUBSTR(tag, 1, INSTR(tag || '.', '.') - 1) as component, 
            COUNT(*) 
        FROM traces 
        GROUP BY component
    """)
    components = c.fetchall()
    if components:
        comp_strs = [f"{comp} ({count})" for comp, count in components if comp]
        print(f"Components exercised: {', '.join(comp_strs)}")
        
    conn.close()

def summarize_level_1(db_path, req_id=None):
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()
    
    query = "SELECT * FROM assertions"
    params = ()
    if req_id:
        query += " WHERE requirement_id = ?"
        params = (req_id,)
    query += " ORDER BY requirement_id, id"
        
    c.execute(query, params)
    rows = c.fetchall()
    
    if not rows:
        print("No assertions found.")
        return
        
    from collections import defaultdict
    by_req = defaultdict(list)
    for r in rows:
        by_req[r['requirement_id']].append(r)
        
    print("Level 1: Detail per requirement")
    print("===============================")
    for req, items in by_req.items():
        print(f"\n{req} ({len(items)} failures):")
        for i, item in enumerate(items, 1):
            print(f"  #{i}: {item['condition']}")
            print(f"      {item['message']}")
            print(f"      At {item['source_file']}:{item['source_line']}")
            
            try:
                call_chain = json.loads(item['call_chain'] or '[]')
                if call_chain:
                    funcs = [c.get('func', 'unknown') for c in call_chain[-5:]] # show last 5
                    print(f"      Recent call chain: {' -> '.join(funcs)}")
            except:
                pass
                
    conn.close()

def main():
    parser = argparse.ArgumentParser(description="Logos Trace Summarizer")
    parser.add_argument("db_path", help="Path to SQLite trace database")
    parser.add_argument("--level", type=int, choices=[0, 1, 2], default=0, help="Summary level (0=overview, 1=req detail, 2=full detail)")
    parser.add_argument("--req", help="Filter by requirement ID (for level 1/2)")
    
    args = parser.parse_args()
    
    if args.level == 0:
        summarize_level_0(args.db_path)
    elif args.level == 1:
        summarize_level_1(args.db_path, args.req)
    else:
        print("Level 2 not implemented in basic summarizer. Use sqlite3 CLI.")

if __name__ == "__main__":
    main()
