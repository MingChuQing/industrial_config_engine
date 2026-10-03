"""Verify complete process supplement, then replay the unchanged recorded response.

Requires only Python's standard library. JSON and CSV are compared with a fresh
baseline-plus-response extraction. The XLSX is checked cell by cell, including
cached numerical results of formulas, without Excel or a model invocation.
"""
import argparse
import csv
import io
import json
import math
from pathlib import Path
import posixpath
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

from build_beverage_process_spec import ROOT, DEST, build_payloads

NS = {'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main',
      'r':'http://schemas.openxmlformats.org/officeDocument/2006/relationships',
      'p':'http://schemas.openxmlformats.org/package/2006/relationships'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def column_name(index):
    result=''
    while index:
        index,remainder=divmod(index-1,26)
        result=chr(65+remainder)+result
    return result


def read_xlsx(path):
    require(Path(path).is_file(), 'Workbook is missing: '+str(path))
    with zipfile.ZipFile(path) as z:
        shared=[]
        if 'xl/sharedStrings.xml' in z.namelist():
            for item in ET.fromstring(z.read('xl/sharedStrings.xml')).findall('s:si',NS):
                shared.append(''.join(t.text or '' for t in item.findall('.//s:t',NS)))
        relationships=ET.fromstring(z.read('xl/_rels/workbook.xml.rels'))
        rels={r.attrib['Id']:r.attrib['Target'] for r in relationships}
        workbook=ET.fromstring(z.read('xl/workbook.xml'))
        result={}
        for s in workbook.findall('s:sheets/s:sheet',NS):
            name=s.attrib['name']; target=rels[s.attrib['{'+NS['r']+'}id']]
            location=target.lstrip('/') if target.startswith('/') else posixpath.normpath(posixpath.join('xl',target))
            xml=ET.fromstring(z.read(location)); cells={}
            for c in xml.findall('.//s:sheetData/s:row/s:c',NS):
                ref=c.attrib['r']; kind=c.attrib.get('t'); element=c.find('s:v',NS)
                value=element.text if element is not None else None
                formula=c.find('s:f',NS)
                if kind=='inlineStr':
                    value=''.join(t.text or '' for t in c.findall('s:is//s:t',NS))
                elif kind=='s':
                    require(value is not None, name+'!'+ref+': empty shared string index')
                    value=shared[int(value)]
                elif kind=='b': value=value=='1'
                elif kind=='e':
                    raise ValueError(name+'!'+ref+': Excel error '+str(value))
                elif kind in ('str','d'):
                    value=value or ''
                elif value is not None:
                    try: value=float(value)
                    except ValueError: raise ValueError(name+'!'+ref+': invalid numeric cache '+repr(value))
                    require(math.isfinite(value),name+'!'+ref+': nonfinite numeric cell')
                else: value=''
                cells[ref]={'value':value,'formula':formula.text if formula is not None else None,
                            'has_formula':formula is not None,'has_cache':element is not None and element.text is not None}
            result[name]=cells
        return result


def compare_cell(actual, expected, label):
    if isinstance(expected,(int,float)) and not isinstance(expected,bool):
        require(isinstance(actual,(int,float)) and not isinstance(actual,bool),label+': expected numeric '+repr(expected)+', got '+repr(actual))
        require(math.isclose(actual,expected,rel_tol=1e-10,abs_tol=1e-10),label+': numeric mismatch '+repr(actual)+' != '+repr(expected))
    else:
        require(actual==expected,label+': mismatch '+repr(actual)+' != '+repr(expected))


def verify_workbook(path, workbook):
    actual=read_xlsx(path)
    expected_names={s['name'] for s in workbook['sheets']}
    require(set(actual)==expected_names,'Workbook sheet names differ: '+repr(set(actual)^expected_names))
    checked=0; formula_cells=0
    for s in workbook['sheets']:
        name=s['name']; cells=actual[name]; allowed=set()
        for col,value in enumerate(s['headers'],1):
            ref=column_name(col)+'5'; allowed.add(ref)
            compare_cell(cells.get(ref,{}).get('value',''),value,name+'!'+ref)
            checked+=1
        for rownum,row in enumerate(s['rows'],6):
            for col,value in enumerate(row,1):
                ref=column_name(col)+str(rownum); allowed.add(ref)
                cell=cells.get(ref,{'value':'','has_formula':False})
                if cell['has_formula']:
                    require(cell.get('has_cache'),name+'!'+ref+': formula cache missing; recalculate/export before publication')
                    formula_cells+=1
                compare_cell(cell['value'],value,name+'!'+ref)
                # Check the formula expression as well as its cached result:
                # stale correct caches must not hide a modified formula.
                expected_formula=None
                if name=='配方参数':
                    expected_formula={6:f'ROUNDUP(1000*D{rownum}/E{rownum},0)',
                                      9:f'G{rownum}*H{rownum}',10:f'D{rownum}*I{rownum}'}.get(col)
                elif name=='增量对比' and col==4:
                    expected_formula=f'1-C{rownum}/B{rownum}'
                if expected_formula:
                    require(cell['has_formula'],name+'!'+ref+': derived column must contain a formula')
                    normalized=re.sub(r'\s+','',cell.get('formula') or '').lstrip('=').upper()
                    require(normalized==expected_formula.upper(),name+'!'+ref+': formula expression mismatch')
                else:
                    require(not cell['has_formula'],name+'!'+ref+': unexpected formula in source data cell')
                checked+=1
        for ref,cell in cells.items():
            match=re.fullmatch(r'([A-Z]+)([1-9][0-9]*)',ref)
            require(match is not None,'Invalid cell reference: '+ref)
            if int(match[2])>=5 and ref not in allowed:
                require(cell['value']=='' and not cell['has_formula'],name+'!'+ref+': unexpected data outside documented table')
    return {'sheets':len(expected_names),'checked_cells':checked,'formula_cells':formula_cells}


def verify_data(directory=DEST):
    directory=Path(directory)
    fresh,csvs=build_payloads()
    for name,expected in fresh.items():
        path=directory/name
        require(path.is_file(),'Missing supplement: '+str(path))
        actual=json.loads(path.read_text(encoding='utf-8-sig'))
        require(json.dumps(actual,ensure_ascii=False,sort_keys=True)==json.dumps(expected,ensure_ascii=False,sort_keys=True),
                'Stale or edited JSON: '+name+'; rebuild from unchanged sources')
    actual_csvs={p.relative_to(directory).as_posix() for p in (directory/'tables').glob('*.csv')}
    require(actual_csvs==set(csvs),'CSV file set differs from generated sheets')
    for name,expected in csvs.items():
        actual_rows=list(csv.reader(io.StringIO((directory/name).read_text(encoding='utf-8-sig'))))
        expected_rows=list(csv.reader(io.StringIO(expected)))
        require(actual_rows==expected_rows,'Stale or edited CSV: '+name)
    return fresh, {'json_files':len(fresh),'csv_files':len(csvs),'source_hashes':len(fresh['source_manifest.json']['files']),
                   'candidate_documents':len(fresh['process_spec.json']['configuration_documents']),
                   'counts':fresh['process_spec.json']['counts']}


def verify(directory=DEST,xlsx=None,no_replay=False):
    fresh,report=verify_data(directory)
    report['workbook']=verify_workbook(xlsx or Path(directory)/'process_spec.xlsx',fresh['workbook_data.json'])
    if not no_replay:
        completed=subprocess.run([sys.executable,'-X','utf8',str(ROOT/'scripts/verify_beverage_nl_extension.py')],
                                 cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
        require(completed.returncode==0,'Recorded replay failed:\n'+completed.stdout+'\n'+completed.stderr)
        try: summary=json.loads(completed.stdout)
        except json.JSONDecodeError: raise ValueError('Recorded replay did not emit its expected JSON summary')
        require(summary.get('status')=='PASS','Recorded replay status not PASS')
        report['recorded_replay']=summary
    else:
        report['recorded_replay']='SKIPPED (--no-replay; documentation checks only)'
    report['status']='PASS'
    report['scope']='Fresh baseline+recorded-response extraction, source hashes, JSON/CSV/XLSX equality; no model invocation or new generation.'
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,default=DEST)
    parser.add_argument('--xlsx',type=Path,help='Workbook path; default process_spec/process_spec.xlsx')
    parser.add_argument('--no-replay',action='store_true',help='Development check of files and workbook without running the archived simulation')
    args=parser.parse_args()
    try:
        print(json.dumps(verify(args.directory,args.xlsx,args.no_replay),ensure_ascii=False,indent=2))
    except (ValueError,KeyError,OSError,zipfile.BadZipFile,ET.ParseError) as error:
        print(json.dumps({'status':'FAIL','error':str(error)},ensure_ascii=False),file=sys.stderr)
        raise SystemExit(1)
