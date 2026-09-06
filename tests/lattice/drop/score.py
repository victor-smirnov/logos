import json,sys,re
import os
cells={c['id']:c for c in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'cells.json')))}
rows={}
for ln in open(sys.argv[1]):
    ln=ln.rstrip('\n')
    p=ln.split('|')
    if len(p)<6: continue
    rows[p[0]]=dict(cc=p[1],diag=p[2],run=p[3],so=p[4],msg=p[5])
out=[]
for cid,c in sorted(cells.items()):
    r=rows.get(cid)
    if r is None: out.append((cid,'MISSING','','')); continue
    want_ref = 'must be REFUSED' in c['why']
    if r['cc']!='0' or r['diag']!='0':
        out.append((cid,'OK' if want_ref else 'REFUSED',r['msg'],'')); continue
    if want_ref:
        out.append((cid,'ADMITS','illegal program compiled; ran rc %s out %s'%(r['run'],r['so']),'')); continue
    if r['run']=='LINKFAIL': out.append((cid,'LINKFAIL',r['msg'],'')); continue
    if r['run']!='0': out.append((cid,'RUNRC',r['run'],r['so'])); continue
    m=re.match(r'count=(-?\d+) value=(-?\d+);',r['so'])
    if not m: out.append((cid,'NOOUT',r['so'],'')); continue
    gc,gv=int(m.group(1)),int(m.group(2))
    if gc==c['expect_count'] and gv==c['expect_value']: out.append((cid,'OK','','')); continue
    k='WRONGCOUNT' if gc!=c['expect_count'] else 'WRONGVALUE'
    out.append((cid,k,'count %d want %d'%(gc,c['expect_count']),'value %d want %d'%(gv,c['expect_value'])))
from collections import Counter
cnt=Counter(o[1] for o in out)
print('TOTAL',len(out),dict(cnt))
for o in out:
    if o[1]!='OK': print('%-40s %-11s %s %s'%(o[0],o[1],o[2],o[3]))
