#!/usr/bin/env python3
"""foundations_lint — anchors in docs/foundations/*.md must point at real code.

Errors (exit 1):
  - an article lacks front matter with `title`, `verified-at`, `sources`;
  - `verified-at` is not a commit of the repository;
  - a relative link does not resolve to a file or directory;
  - a symbol anchor [`A::b`](path) names a file in which `b` does not occur;
  - an article is missing from README.md's index, or the index lists a missing file.

Warnings (exit 0): a file anchored by an article changed after its `verified-at`.
The article may describe old code; re-verify and move `verified-at`.

The lint checks anchors, not claims. See docs/foundations/README.md "Anchors and the lint".

Usage: foundations_lint.py SRC_ROOT [--no-stale]
"""
import os
import re
import subprocess
import sys

LINK = re.compile(r'\[([^\]]*)\]\(([^)\s]+)\)')
SYMBOL_TEXT = re.compile(r'^`([A-Za-z_][\w:.<>]*)`$')
FENCE = re.compile(r'^(```|~~~)')


def git(root, *args):
    return subprocess.run(['git', '-C', root, *args], capture_output=True, text=True)


def front_matter(text):
    if not text.startswith('---\n'):
        return None
    end = text.find('\n---\n', 4)
    if end < 0:
        return None
    fields = {}
    for line in text[4:end].splitlines():
        if ':' in line:
            key, val = line.split(':', 1)
            fields[key.strip()] = val.strip()
    return fields


def links(text):
    """Links outside fenced code blocks, as (line_no, text, target)."""
    in_fence = False
    for no, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line.strip()):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        # drop inline code spans that are not link texts, so `[x](y)` inside code is ignored
        for m in LINK.finditer(line):
            yield no, m.group(1), m.group(2)


def last_segment(name):
    name = re.sub(r'<.*>', '', name)
    return re.split(r'::|\.', name)[-1]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    root = os.path.abspath(argv[1])
    check_stale = '--no-stale' not in argv
    fdir = os.path.join(root, 'docs', 'foundations')
    errors, warnings = [], []
    articles = sorted(f for f in os.listdir(fdir) if f.endswith('.md') and f != 'README.md')

    readme = open(os.path.join(fdir, 'README.md'), encoding='utf-8').read()
    indexed = set()
    for _, _, target in links(readme):
        t = target.split('#')[0]
        if t.endswith('.md') and '/' not in t:
            indexed.add(t)
    for a in articles:
        if a not in indexed:
            errors.append(f'README.md: article {a} is not in the index')
    for t in sorted(indexed):
        if not os.path.exists(os.path.join(fdir, t)):
            errors.append(f'README.md: index lists {t}, which does not exist')

    for name in ['README.md'] + articles:
        path = os.path.join(fdir, name)
        text = open(path, encoding='utf-8').read()
        anchored = set()
        commit = None
        if name != 'README.md':
            fm = front_matter(text)
            if fm is None:
                errors.append(f'{name}: no front matter')
            else:
                for key in ('title', 'verified-at', 'sources'):
                    if not fm.get(key):
                        errors.append(f'{name}: front matter lacks `{key}`')
                commit = fm.get('verified-at')
                if commit and git(root, 'cat-file', '-e', commit + '^{commit}').returncode != 0:
                    errors.append(f'{name}: verified-at {commit} is not a commit')
                    commit = None
        for no, ltext, target in links(text):
            if re.match(r'^[a-z]+:', target) or target.startswith('#'):
                continue
            rel = target.split('#')[0]
            dest = os.path.normpath(os.path.join(fdir, rel))
            if not os.path.exists(dest):
                errors.append(f'{name}:{no}: link target {target} does not exist')
                continue
            if dest.startswith(root + os.sep):
                anchored.add(os.path.relpath(dest, root))
            m = SYMBOL_TEXT.match(ltext.strip())
            if m and os.path.isfile(dest):
                seg = last_segment(m.group(1))
                body = open(dest, encoding='utf-8', errors='replace').read()
                if not re.search(r'(?<![\w])' + re.escape(seg) + r'(?![\w])', body):
                    errors.append(f'{name}:{no}: symbol `{m.group(1)}` ({seg}) not found in {rel}')
        if check_stale and commit and anchored:
            changed = git(root, 'diff', '--name-only', commit, 'HEAD', '--', *sorted(anchored)).stdout.split()
            if changed:
                warnings.append(f'{name}: {len(changed)} anchored path(s) changed since {commit[:10]}: '
                                + ', '.join(changed[:8]) + (' ...' if len(changed) > 8 else ''))

    for w in warnings:
        print('warning:', w)
    for e in errors:
        print('error:', e)
    print(f'foundations_lint: {len(articles)} article(s), {len(errors)} error(s), {len(warnings)} warning(s)')
    return 1 if errors else 0


def selftest():
    """Plant one defect of each kind in a scratch repo; every one must be reported."""
    import tempfile
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, 'docs', 'foundations'))
        os.makedirs(os.path.join(root, 'src'))
        open(os.path.join(root, 'src', 'a.cpp'), 'w').write('void present() {}\n')
        git(root, 'init', '-q')
        git(root, '-c', 'user.name=t', '-c', 'user.email=t@t', 'add', '.')
        git(root, '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'x')
        head = git(root, 'rev-parse', 'HEAD').stdout.strip()
        f = os.path.join(root, 'docs', 'foundations')
        open(os.path.join(f, 'README.md'), 'w').write('[good](good.md) [gone](gone.md)\n')
        open(os.path.join(f, 'good.md'), 'w').write(
            f'---\ntitle: g\nverified-at: {head}\nsources: []\n---\n'
            '[`present`](../../src/a.cpp) [`absent`](../../src/a.cpp) [x](../../src/nope.cpp)\n')
        open(os.path.join(f, 'unlisted.md'), 'w').write('no front matter\n')
        import io, contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = main(['', root])
        text = out.getvalue()
        expect = ['unlisted.md is not in the index', 'gone.md, which does not exist',
                  'unlisted.md: no front matter', 'symbol `absent`', 'nope.cpp does not exist']
        missing = [e for e in expect if e not in text]
        if rc != 1 or missing or 'symbol `present`' in text:
            print(text)
            print('selftest FAILED; not reported:', missing)
            return 1
    return 0


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        if selftest():
            sys.exit(1)
        sys.argv.remove('--selftest')
    sys.exit(main(sys.argv))
