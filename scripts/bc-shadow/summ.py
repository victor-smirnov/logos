import sys,re,collections
lines=open(sys.argv[1],errors='replace').read().split('\n')
recs=[];cur=None;cen=collections.Counter()
for ln in lines:
    if ln.startswith('bc-census'):
        for kv in ln.split('\t')[2:]:
            k,v=kv.split('='); cen[k]+=int(v)
    if ln.startswith('bc\t'):
        p=ln.split('\t'); cur={'kind':p[1],'fn':p[2],'input':'','old':[],'new':[]}; recs.append(cur)
    elif cur and ln.startswith('  input: '): cur['input']=ln[9:]
    elif cur and ln.startswith('  old: '): cur['old'].append(ln[7:])
    elif cur and ln.startswith('  new: '): cur['new'].append(ln[7:])
    elif not ln.startswith('  '): cur=None
print(dict(cen))
def norm(m): return re.sub(r"'[^']*'","'X'",re.sub(r"line \d+","line N",m))
byfn={}
for r in recs:
    k=(r['kind'],r['fn']); d=byfn.setdefault(k,{'n':0,'msgs':set(),'inputs':set()}); d['n']+=1; d['inputs'].add(r['input'])
    for m in (r['new'] if r['kind']=='new_only' else r['old']): d['msgs'].add(m)
print("distinct fns: new_only",sum(1 for k in byfn if k[0]=='new_only'),"old_only",sum(1 for k in byfn if k[0]=='old_only'))
cls=collections.Counter()
for (kind,fn),d in byfn.items():
    for m in d['msgs']: cls[(kind,norm(m))]+=1
for (kind,m),c in cls.most_common(int(sys.argv[2]) if len(sys.argv)>2 else 12): print(c,kind,m[:140])
