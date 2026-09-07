import io,re,sys
def kinds(path, fname):
    s=io.open(path,encoding='utf-8').read().split('\n')
    st=None
    for i,l in enumerate(s):
        if fname in l and '(' in l and not l.strip().startswith('//'): st=i; break
    if st is None: return None
    d=0; started=False; out=[]
    for l in s[st:]:
        d+=l.count('{')-l.count('}')
        if '{' in l: started=True
        out += re.findall(r'Code::(\w+)', l)
        if started and d<=0: break
    return sorted(set(out))
for p,f in [('src/compiler/mlir_gen_stmt.cpp','MLIRGenImpl::pat_bind'),
            ('src/compiler/mlir_gen_stmt.cpp','collect_pat_bindings'),
            ('src/compiler/sema_stmt.cpp','SemaChecker::bind_pattern_ref'),
            ('src/compiler/borrow_check.cpp','void declare_pat_bindings(lir_view::PatRef pr)')]:
    print(f, kinds(p,f))
