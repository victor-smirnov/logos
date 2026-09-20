import sys,collections
lines=open(sys.argv[1],errors='replace').read().split('\n')
recs=[];cur=None
for ln in lines:
    if ln.startswith('bc\t'):
        p=ln.split('\t'); cur={'kind':p[1],'fn':p[2],'input':'','new':[],'old':[]}; recs.append(cur)
    elif cur and ln.startswith('  input: '): cur['input']=ln[9:]
    elif cur and ln.startswith('  new: '): cur['new'].append(ln[7:])
    elif cur and ln.startswith('  old: '): cur['old'].append(ln[7:])
    elif not ln.startswith('  '): cur=None
kind=sys.argv[2]; pat=sys.argv[3]
c=collections.Counter(); ex={}
for r in recs:
    if r['kind']!=kind: continue
    for m in (r['new'] if kind=='new_only' else r['old']):
        if pat in m:
            key=(r['fn'] if r['fn']!='main' else 'main@'+r['input'].split('/')[-1])
            c[key]+=1; ex.setdefault(key,(m,r['input']))
for fn,n in c.most_common(int(sys.argv[4]) if len(sys.argv)>4 else 12): print(n,fn[:85],'|',ex[fn][0][:95],'|',ex[fn][1].split('/')[-1])
