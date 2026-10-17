#!/usr/bin/env python3
# Runs OXP over the W3C XML Conformance Test Suite (xmlts20130923), which is
# test data only and never kept in this repository: give its unpacked
# xmlconf/ folder.
#   python3 -I oxp/tests/w3c/run-w3c.py XMLCONF_DIR [OXPCANON] [--list]
# In scope: XML 1.0 fifth edition and Namespaces in XML 1.0, a non-validating
# parser that reads no external entity. Out of scope, counted apart: XML 1.1,
# tests for earlier editions only, and tests whose outcome depends on an
# external entity or the external subset (ENTITIES other than "none") where
# OXP's answer can't show it.
#   valid, invalid  must be well-formed; with an OUTPUT file and no external
#                   entities, the canonical form must match it too
#   not-wf          must be refused
#   error           reported, not scored (the processor may go either way)
# Needs lxml (for the catalog's external entities).
# MIT licence. Copyright (c) 2026 AmigaChrome Team.
import os, subprocess, sys, collections, urllib.parse
from lxml import etree

conf = os.path.abspath(sys.argv[1])
canon = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else os.path.join(os.path.dirname(__file__), '../../build/oxpcanon')
listing = '--list' in sys.argv

parser = etree.XMLParser(resolve_entities=True, load_dtd=True, no_network=True)
root = etree.parse(os.path.join(conf, 'xmlconf.xml'), parser).getroot()

def path_of(el, rel):
    base = el.base or ''
    u = urllib.parse.urljoin(base, rel)
    if u.startswith('file://'): u = urllib.parse.unquote(u[7:])
    return u

stats = collections.defaultdict(collections.Counter)
fails = []
for t in root.iter('TEST'):
    a = t.attrib
    rec = a.get('RECOMMENDATION', 'XML1.0')
    ed = a.get('EDITION')
    sect = os.path.relpath(path_of(t, a['URI']), conf).split(os.sep)
    sect = '/'.join(sect[:2]) if sect[0] == 'eduni' else sect[0]
    kind = a['TYPE']
    if rec in ('XML1.1', 'NS1.1') or a.get('VERSION') == '1.1':
        stats[sect]['skip: XML 1.1'] += 1; continue
    if ed and '5' not in ed.split():
        stats[sect]['skip: earlier edition'] += 1; continue
    f = path_of(t, a['URI'])
    if not os.path.exists(f):
        # a few TESTCASES carry an xml:base that does not match where their files are
        # (eduni/misc's under eduni/namespaces/misc/): look for the folder and file by name
        parts = f.split(os.sep)
        hits = [os.path.join(d, parts[-1]) for d, _, fs in os.walk(conf) if os.path.basename(d) == parts[-2] and parts[-1] in fs]
        if len(hits) == 1: f = hits[0]
    args = [canon] + (['-n'] if a.get('NAMESPACE') == 'no' else []) + [f]
    r = subprocess.run(args, capture_output=True)
    ok = r.returncode == 0
    ext = a.get('ENTITIES', 'none') != 'none'
    tid = a.get('ID')
    if kind in ('valid', 'invalid'):
        if not ok:
            stats[sect]['FAIL (refused a well-formed one)'] += 1
            fails.append((sect, tid, kind, f, r.stderr.decode(errors='replace').strip()))
            continue
        out = a.get('OUTPUT')
        if out and not ext:
            exp = open(os.path.join(os.path.dirname(f), os.path.relpath(path_of(t, out), os.path.dirname(path_of(t, a['URI'])))), 'rb').read()
            # the second canonical form also lists the notations in a DOCTYPE, and puts the
            # internal subset's PIs before it; OXP reports neither (by design), so only the
            # document after the DOCTYPE is compared
            if b'<!DOCTYPE' in exp:
                exp = exp[exp.index(b']>\n') + 3:]
            if exp != r.stdout:
                stats[sect]['FAIL (output differs)'] += 1
                fails.append((sect, tid, kind, f, 'output differs'))
                continue
            stats[sect]['pass (and output)'] += 1
        else:
            stats[sect]['pass'] += 1
    elif kind == 'not-wf':
        if ok and ext:
            stats[sect]['out of scope: the error is in an external entity'] += 1
        elif ok:
            stats[sect]['FAIL (accepted a not-well-formed one)'] += 1
            fails.append((sect, tid, kind, f, ''))
        else:
            stats[sect]['pass'] += 1
    else:
        stats[sect]['error test: ' + ('accepted' if ok else 'refused')] += 1

total = collections.Counter()
for s in sorted(stats):
    print(s)
    for k, v in sorted(stats[s].items()):
        print('   %5d  %s' % (v, k)); total[k] += v
print('all')
for k, v in sorted(total.items()):
    print('   %5d  %s' % (v, k))
if listing:
    for x in fails: print('FAIL', *x)
