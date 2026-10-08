"""Receiver gets acquisition IQ only. This process owns all Golden comparisons."""
import argparse,csv,json,subprocess,time,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def table(path):
    with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def write(path,rows):
    if not rows:return
    columns=list(dict.fromkeys(key for row in rows for key in row))
    with path.open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,columns);w.writeheader();w.writerows(rows)
def main():
    p=argparse.ArgumentParser();p.add_argument('--mode',default='fast',choices=['fast','deep2','deep3','host'])
    p.add_argument('--run-name',default='');p.add_argument('--candidate');p.add_argument('--reuse',action='store_true');p.add_argument('--executable',type=Path,
        default=ROOT/'results_c_validation/final/build/wrj_c99_host.exe');a=p.parse_args()
    base=ROOT/'results_c_validation/host53';inputs=base/'inputs';gold=base/'golden_comparator_only'
    refs={r['candidateId']:r for r in table(gold/'receiver_parse_results.csv')}
    merged={r['Signal']:r for r in table(gold/'merged_end_to_end_cases.csv')}
    assert not a.run_name or a.run_name.replace('_','').isalnum(), 'run name must be a simple artifact label'
    prefix=(a.run_name+'_') if a.run_name else ''
    received=[];runs=[]
    prior=[]
    summary_path=base/(prefix+a.mode+'_summary.csv')
    if a.candidate and summary_path.exists():prior=[r for r in table(summary_path) if r['candidate']!=a.candidate]
    for meta in table(inputs/'handoff.csv'):
        cid=meta['candidateId']
        if a.candidate and a.candidate!=cid:continue
        actual=inputs/(cid+'.cf32');sha=hashlib.sha256(actual.read_bytes()).hexdigest().upper()
        assert sha==merged[cid]['InputSHA256'].upper(),cid+' IQ differs from formal M2 input'
        output=base/('c_'+prefix+a.mode)/cid;output.mkdir(parents=True,exist_ok=True)
        command=[str(a.executable.resolve()),a.mode,str(inputs/'handoff.csv'),cid,str(actual),str(output)]
        assert not any('golden_comparator_only' in v for v in command)
        start=time.perf_counter()
        if not a.reuse:
            try:
                result=subprocess.run(command,capture_output=True,text=True,timeout=1800)
                (output/'run.log').write_text(result.stdout+result.stderr,encoding='utf-8');code=result.returncode
            except subprocess.TimeoutExpired as error:
                (output/'run.log').write_text(str(error),encoding='utf-8');code=-1
            if code:raise RuntimeError(f'{cid} host failed exit={code}; inspect {output}/run.log')
        row=table(output/'summary.csv')[0];row['GoldenFinalComplete']=refs[cid]['parseComplete'];row['InputSHA256']=sha
        row['GoldenFastStatus']=merged[cid]['M4FastStatus']
        row['GoldenFastStatusMatches']=(int(row['M4FastComplete'])==int(merged[cid]['M4FastStatus']=='COMPLETE'))
        row['SyncPlusFlowSuccess']=int(row.get('M5Success',row['complete']))*int(row.get('InitialM3Accepted',row['M3SyncAccepted']))
        row['GoldenFinalStatusMatches']=str(int(row['complete']))==str(int(float(refs[cid]['parseComplete'])))
        received.append(row);runs.append({'candidate':cid,'mode':a.mode,'wallSeconds':time.perf_counter()-start})
        write(summary_path,prior+received);write(base/(prefix+a.mode+'_runtime.csv'),runs)
        print(f"{cid}: {row['M4Status']} family={row['family']} records={row['records']} stage={row['failureStage']} seconds={runs[-1]['wallSeconds']:.2f}",flush=True)
    print(json.dumps({'executed':len(received),'CComplete':sum(int(r['complete']) for r in received),
        'comparison':'Stage and status progress; byte/field acceptance requires compare_e34_host.py','M1M2Runs':0,'mode':a.mode},indent=2),flush=True)
if __name__=='__main__':main()
