#!/usr/bin/env python3
"""Parse ESP8266 target-side P50/P95/P99/P999 timing evidence."""
from __future__ import annotations
import argparse, hashlib, json, math, re, sys, tempfile
from pathlib import Path
from typing import Any
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'/'lib'))
from ev_redaction import redact_text
DEFAULT_TARGET='wemos_esp_wroom_02_18650'
BUDGET_FILE=ROOT/'config'/'target_timing_budgets.json'
DEFAULT_OUTPUT=ROOT/'docs'/'release'/'target_timing'/DEFAULT_TARGET/'current'
IDF_TS_RE=re.compile(r'^[A-Z]\s*\(\s*(?P<ms>\d+)\s*\)')
TICK_RE=re.compile(r'EV_WEMOS_SMOKE_TICK\s+seq=(?P<seq>\d+)')
SNAP_RE=re.compile(r'EV_WEMOS_SMOKE_SNAPSHOT\s+seq=(?P<seq>\d+)')
EXPLICIT_SAMPLE_RE=re.compile(r'EV_TARGET_TIMING_SAMPLE\s+name=(?P<name>[A-Za-z0-9_\-]+)\s+seq=(?P<seq>\d+)\s+us=(?P<us>\d+)')
RESET_FAIL_RE=re.compile(r'\bpanic\b|\bfatal\b|\bexception\b|wdt\s+reset|watchdog|abort\(|EV_\w+_FAIL',re.I)
OPERATOR_STOP_RE=re.compile(r'process exited with code 130|EV_MONITOR_STOP\s+reason=operator_sigint|\^C',re.I)
SECRET_RE=re.compile(r'(WIFI_PASSWORD|COMMAND_TOKEN|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\S*',re.I)
PLACEHOLDER_RE=re.compile(r'(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/',re.I)
DEFAULT_BUDGETS={'min_samples':8,'max_gap_ms_warning':2500,'max_gap_ms_hard':10000,'p99_report_only':True,'p999_report_only':True}
def redact(text:str)->str: return redact_text(text)
def sha256_file(path:Path)->str:
    h=hashlib.sha256()
    with path.open('rb') as fh:
        for chunk in iter(lambda:fh.read(65536),b''): h.update(chunk)
    return h.hexdigest()
def rel(path:Path)->str:
    try: return path.relative_to(ROOT).as_posix()
    except ValueError: return str(path)
def is_placeholder_path(path:Path|str|None)->bool: return path is not None and bool(PLACEHOLDER_RE.search(str(path)))
def safe_read(path:Path)->tuple[str,str,str]:
    if is_placeholder_path(path): return 'ENVIRONMENT_BLOCKED',f'placeholder path was supplied: {path}',''
    try:
        if not path.exists(): return 'ENVIRONMENT_BLOCKED',f'serial timing log not found: {path}',''
        if path.is_dir(): return 'FAIL',f'input path is directory, expected serial log: {path}',''
        return 'PASS','',path.read_text(encoding='utf-8',errors='ignore')
    except PermissionError as exc: return 'FAIL',f'unable to read serial timing log: {path}: {exc}',''
    except OSError as exc: return 'FAIL',f'unable to read serial timing log: {path}: {exc}',''
def load_budgets()->dict[str,Any]:
    if not BUDGET_FILE.is_file(): return dict(DEFAULT_BUDGETS)
    try:
        data=json.loads(BUDGET_FILE.read_text(encoding='utf-8',errors='ignore'))
        return {**DEFAULT_BUDGETS, **(data if isinstance(data,dict) else {})}
    except Exception: return dict(DEFAULT_BUDGETS)
def percentile(values:list[int],pct:float)->int|None:
    if not values: return None
    o=sorted(values); idx=max(0,min(len(o)-1,math.ceil((pct/100.0)*len(o))-1)); return o[idx]
def summary_ms(values:list[int])->dict[str,Any]:
    return {'count':len(values),'min_ms':min(values) if values else None,'p50_ms':percentile(values,50.0),'p95_ms':percentile(values,95.0),'p99_ms':percentile(values,99.0),'p999_ms':percentile(values,99.9),'max_ms':max(values) if values else None}
def summary_us(values:list[int])->dict[str,Any]:
    base={'count':len(values),'min_us':min(values) if values else None,'p50_us':percentile(values,50.0),'p95_us':percentile(values,95.0),'p99_us':percentile(values,99.0),'p999_us':percentile(values,99.9),'max_us':max(values) if values else None}
    base.update({k.replace('_us','_ms'):(v/1000.0 if isinstance(v,int) else None) for k,v in list(base.items()) if k.endswith('_us')})
    return base
def increasing(xs:list[int])->bool: return all(b>a for a,b in zip(xs,xs[1:]))
def parse_text(text:str,*,target:str,min_samples:int|None=None)->dict[str,Any]:
    text=redact(text); budgets=load_budgets(); min_samples=int(min_samples or budgets.get('min_samples',8)); max_hard=int(budgets.get('max_gap_ms_hard',10000)); max_warn=int(budgets.get('max_gap_ms_warning',2500))
    tick=[]; snap=[]; explicit={}; missing_ts=0; ts_seen=False; reset=RESET_FAIL_RE.search(text) is not None
    for raw in text.splitlines():
        ts=IDF_TS_RE.search(raw); ms=int(ts.group('ms')) if ts else None; ts_seen=ts_seen or bool(ts)
        if m:=EXPLICIT_SAMPLE_RE.search(raw): explicit.setdefault(m.group('name').replace('-','_'),[]).append((int(m.group('seq')),int(m.group('us'))))
        if m:=TICK_RE.search(raw):
            if ms is None: missing_ts+=1
            else: tick.append({'seq':int(m.group('seq')),'ms':ms})
        if m:=SNAP_RE.search(raw):
            if ms is None: missing_ts+=1
            else: snap.append({'seq':int(m.group('seq')),'ms':ms})
    failures=[]; warnings=[]; metrics={}
    if reset: failures.append('reset/failure marker observed')
    for name, arr in [('tick',tick),('snapshot',snap)]:
        if arr and not increasing([x['seq'] for x in arr]): failures.append(f'{name} sequence is not strictly increasing')
        if arr and not increasing([x['ms'] for x in arr]): failures.append(f'{name} timestamps are not strictly increasing')
    ti=[b['ms']-a['ms'] for a,b in zip(tick,tick[1:])]; si=[b['ms']-a['ms'] for a,b in zip(snap,snap[1:])]
    if ti: metrics['smoke_tick_interval']=summary_ms(ti)
    if si: metrics['smoke_snapshot_interval']=summary_ms(si)
    by_t={x['seq']:x['ms'] for x in tick}; by_s={x['seq']:x['ms'] for x in snap}; common=sorted(set(by_t)&set(by_s)); t2s=[by_s[s]-by_t[s] for s in common if by_s[s]>=by_t[s]]
    if t2s: metrics['event_tick_to_snapshot']=summary_ms(t2s)
    for name,pairs in sorted(explicit.items()):
        if not increasing([s for s,_ in pairs]): failures.append(f'non-monotonic EV_TARGET_TIMING_SAMPLE sequence for {name}')
        metrics[name]=summary_us([us for _,us in pairs])
    total=max(len(tick),len(snap),max((len(v) for v in explicit.values()),default=0)); max_gaps=[v.get('max_ms') for v in metrics.values() if isinstance(v.get('max_ms'),int)]
    if max_gaps and max(max_gaps)>max_hard: failures.append(f'max timing gap exceeds hard threshold: {max(max_gaps)} > {max_hard} ms')
    elif max_gaps and max(max_gaps)>max_warn: warnings.append(f'max timing gap exceeds warning threshold: {max(max_gaps)} ms')
    if missing_ts and not ts_seen and not explicit: status='ENVIRONMENT_BLOCKED'; reason='INSUFFICIENT_TIMING_DATA: timing markers exist but IDF timestamps are missing'
    elif failures: status='FAIL'; reason='; '.join(failures)
    elif total<min_samples: status='INSUFFICIENT_SAMPLES'; reason=f'insufficient target timing samples: {total} < {min_samples}'
    elif not metrics: status='ENVIRONMENT_BLOCKED'; reason='INSUFFICIENT_TIMING_DATA: no timing samples observed'
    else: status='PASS'; reason='target timing evidence parsed from ESP8266 serial timestamps'
    primary=metrics.get('smoke_tick_interval') or metrics.get('event_tick_to_snapshot') or next(iter(metrics.values()),{})
    return {'evidence_kind':'esp8266_target_timing','target':target,'status':status,'reason':reason,'metrics':metrics,'sample_count':total,'tick_count':len(tick),'snapshot_count':len(snap),'timestamp_prefix_seen':ts_seen,'missing_idf_timestamps':missing_ts,'reset_failure_seen':reset,'p50_ms':primary.get('p50_ms'),'p95_ms':primary.get('p95_ms'),'p99_ms':primary.get('p99_ms'),'p999_ms':primary.get('p999_ms'),'p99_report_only':bool(budgets.get('p99_report_only',True)),'p999_report_only':bool(budgets.get('p999_report_only',True)),'operator_interrupt_classification':'CONTROLLED_MONITOR_STOP' if OPERATOR_STOP_RE.search(text) else 'NONE','warnings':warnings,'failures':failures}
def write_outputs(result:dict[str,Any],raw_text:str,evidence_dir:Path,source_log:Path)->None:
    evidence_dir.mkdir(parents=True,exist_ok=True); serial=evidence_dir/'serial.redacted.log'; serial.write_text(redact(raw_text),encoding='utf-8')
    result=dict(result); result['source_log']=rel(source_log); result['source_serial_log_sha256']=sha256_file(source_log); result['redacted_log_sha256']=sha256_file(serial)
    parsed=evidence_dir/'target_timing.json'; parsed.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n',encoding='utf-8'); (evidence_dir/'parsed.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    lines=['# ESP8266 target P99/P999 timing evidence','','| Field | Value |','|---|---|',f"| Status | {result.get('status')} |",f"| Target | {result.get('target')} |",f"| Source log | `{result.get('source_log')}` |",f"| Source SHA-256 | `{result.get('source_serial_log_sha256')}` |",f"| Samples | {result.get('sample_count')} |",f"| P50/P95/P99/P999 ms | {result.get('p50_ms')} / {result.get('p95_ms')} / {result.get('p99_ms')} / {result.get('p999_ms')} |",f"| Reason | {result.get('reason')} |",'','| Metric | Count | P50 ms | P95 ms | P99 ms | P999 ms | Max |','|---|---:|---:|---:|---:|---:|---:|']
    for name,data in (result.get('metrics') or {}).items(): lines.append(f"| {name} | {data.get('count')} | {data.get('p50_ms')} | {data.get('p95_ms')} | {data.get('p99_ms')} | {data.get('p999_ms')} | {data.get('max_ms',data.get('max_us'))} |")
    lines.append('\nP99/P999 are target-side evidence. They do not replace Wemos smoke, SDK build, flash or deep-sleep PASS.'); (evidence_dir/'target_timing_report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    sha=[]
    for p in sorted(evidence_dir.iterdir()):
        if p.is_file() and p.name!='sha256sums.txt': sha.append(f'{sha256_file(p)}  {p.name}')
    (evidence_dir/'sha256sums.txt').write_text('\n'.join(sha)+'\n',encoding='utf-8')
def serial_from_one_shot(d:Path)->Path|None:
    for name in ['serial.raw.log','serial.normalized.log','serial.log']:
        p=d/name
        if p.is_file(): return p
    return None
def parse_cli(args:argparse.Namespace)->int:
    out=args.output_dir or DEFAULT_OUTPUT
    if args.from_one_shot_dir:
        d=args.from_one_shot_dir if args.from_one_shot_dir.is_absolute() else ROOT/args.from_one_shot_dir
        if is_placeholder_path(d): print(f'ESP8266_TARGET_TIMING ENVIRONMENT_BLOCKED: placeholder path was supplied: {d}'); return 77
        if not (d/'manifest.json').is_file(): print(f"ESP8266_TARGET_TIMING ENVIRONMENT_BLOCKED: one-shot manifest not found: {d/'manifest.json'}"); return 77
        log=serial_from_one_shot(d); out=d
        if log is None: print(f'ESP8266_TARGET_TIMING ENVIRONMENT_BLOCKED: one-shot bundle lacks serial log: {d}'); return 77
    elif args.serial_log: log=args.serial_log if args.serial_log.is_absolute() else ROOT/args.serial_log
    else: print('ESP8266_TARGET_TIMING ENVIRONMENT_BLOCKED: --serial-log or --from-one-shot-dir required'); return 77
    out=out if out.is_absolute() else ROOT/out; st,reason,text=safe_read(log)
    if st!='PASS': print(f'ESP8266_TARGET_TIMING {st}: {reason}'); return 77 if st=='ENVIRONMENT_BLOCKED' else 1
    res=parse_text(text,target=args.target,min_samples=args.min_samples); write_outputs(res,text,out,log)
    if args.from_one_shot_dir:
        mp=out/'manifest.json'
        try:
            data=json.loads(mp.read_text(encoding='utf-8',errors='ignore')); data['target_timing']={'status':res.get('status'),'path':'target_timing.json','sha256':sha256_file(out/'target_timing.json'),'samples':res.get('sample_count'),'p99_ms':res.get('p99_ms'),'p999_ms':res.get('p999_ms')}; mp.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n',encoding='utf-8')
        except Exception: pass
    print(f"ESP8266_TARGET_TIMING {res['status']} target={args.target} samples={res['sample_count']}")
    return 0 if res['status']=='PASS' else (77 if res['status'] in {'ENVIRONMENT_BLOCKED','INSUFFICIENT_SAMPLES'} else 1)
def self_test()->int:
    valid=[]
    for i in range(1,11): valid += [f'I ({1000+i*100}) ev_wroom02: EV_WEMOS_SMOKE_TICK seq={i}',f'I ({1002+i*100}) ev_wroom02: EV_WEMOS_SMOKE_SNAPSHOT seq={i} pending=0']
    valid += ['^C','[process exited with code 130 (0x00000082)]']; valid='\n'.join(valid); assert parse_text(valid,target=DEFAULT_TARGET)['status']=='PASS'; assert parse_text(valid,target=DEFAULT_TARGET)['p99_ms']==100
    assert parse_text(valid+'\nI (3000) ev_wroom02: EV_WEMOS_SMOKE_TICK seq=3\n',target=DEFAULT_TARGET)['status']=='FAIL'
    assert parse_text(valid+'\nI (4000) ev_wroom02: panic: boom\n',target=DEFAULT_TARGET)['status']=='FAIL'
    assert parse_text('EV_WEMOS_SMOKE_TICK seq=1\nEV_WEMOS_SMOKE_SNAPSHOT seq=1\n',target=DEFAULT_TARGET)['status']=='ENVIRONMENT_BLOCKED'
    assert parse_text('I (1) ev_wroom02: EV_WEMOS_SMOKE_TICK seq=1\nI (2) ev_wroom02: EV_WEMOS_SMOKE_SNAPSHOT seq=1\n',target=DEFAULT_TARGET)['status']=='INSUFFICIENT_SAMPLES'
    explicit='\n'.join(f'I ({i}) ev: EV_TARGET_TIMING_SAMPLE name=qos_latest_replace seq={i} us={10+i}' for i in range(1,9)); assert parse_text(explicit,target=DEFAULT_TARGET)['status']=='PASS'
    with tempfile.TemporaryDirectory(dir=str(ROOT/'build' if (ROOT/'build').is_dir() else ROOT)) as td:
        d=Path(td); one=d/'one'; one.mkdir(); (one/'manifest.json').write_text(json.dumps({'status':'PASS_SMOKE_ONLY'})+'\n'); (one/'serial.raw.log').write_text(valid)
        assert parse_cli(argparse.Namespace(serial_log=None,from_one_shot_dir=one,target=DEFAULT_TARGET,output_dir=None,min_samples=8))==0; assert (one/'target_timing.json').is_file()
    assert safe_read(Path('/path/to/serial.log'))[0]=='ENVIRONMENT_BLOCKED'; assert 'supersecret' not in redact('WIFI_PASSWORD=supersecret')
    print('ESP8266_TARGET_TIMING_SELF_TEST PASS'); return 0
def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('--self-test',action='store_true'); ap.add_argument('--serial-log','--log',dest='serial_log',type=Path); ap.add_argument('--from-one-shot-dir',type=Path); ap.add_argument('--target',default=DEFAULT_TARGET); ap.add_argument('--output-dir',type=Path); ap.add_argument('--min-samples',type=int,default=None); args=ap.parse_args()
    if args.self_test: return self_test()
    return parse_cli(args)
if __name__=='__main__': raise SystemExit(main())
