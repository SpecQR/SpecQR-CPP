#!/usr/bin/env python3
"""Convert SpecQR C# upstream GS1 fixture JSON into dependency-free C++ tests.
Usage: python3 tests/tools/generate_gs1_fixtures.py path/to/gs1-upstream.json
The fixture data derives from user-owned MIT-licensed SpecQR commit
15ad15e5c770ea0e39072f8f88b2733018f02ffd. No QR implementation is imported.
"""
import json
import pathlib
import sys

def q(s):
    literal = '"' + ''.join(chr(c) if 32 <= c < 127 and chr(c) not in '\\"' else '\\%03o' % c for c in s.encode('utf-8')) + '"'
    return 'std::string(' + literal + ',' + str(len(s.encode('utf-8'))) + ')' if '\0' in s else literal

def es(elements):
    return 'std::vector<gs1::Element>{' + ','.join('{' + q(e['ai']) + ',' + q(e['value']) + '}' for e in elements) + '}'

def string_list(items):
    return 'std::vector<std::string>{' + ','.join(q(x) for x in items) + '}'

def parsed_checks(expected, variable='r'):
    out = ['check(' + variable + '.elements == ' + es(expected['elements']) + ', "elements");']
    if 'hasSeparators' in expected:
        out.append('check(' + variable + '.has_separators == ' + str(expected['hasSeparators']).lower() + ', "separators");')
    if 'primary' in expected:
        out += ['check(' + variable + '.primary == gs1::Element{' + q(expected['primary']['ai']) + ',' + q(expected['primary']['value']) + '}, "primary");',
                'check(' + variable + '.path_elements == ' + es(expected['pathElements']) + ', "path elements");',
                'check(' + variable + '.query_elements == ' + es(expected['queryElements']) + ', "query elements");',
                'check(' + variable + '.unknown_query == std::vector<gs1::UnknownQuery>{' + ','.join('{' + q(x['key']) + ',' + q(x['value']) + '}' for x in expected['unknownQuery']) + '}, "unknown query");']
    return out

def option_setup(op, opts):
    if op in ('validateElements', 'validateRaw'):
        return ['gs1::ValidationOptions o;', 'o.allow_unsupported_ai=' + str(opts.get('allowUnsupportedAi',False)).lower() + ';', 'o.collect_all_errors=' + str(opts.get('collectAllErrors',True)).lower() + ';', 'o.context=gs1::ValidationContext::' + ('DigitalLink' if opts.get('context') == 'digital-link' else 'ElementString') + ';']
    if op == 'linkCreate':
        out = ['gs1::DigitalLinkOptions o;', 'o.base_url=' + q(opts.get('baseUrl','')) + ';', 'o.primary_ai=' + q(opts.get('primaryAi','01')) + ';']
        if 'pathAis' in opts: out.append('o.path_ais=' + string_list(opts['pathAis']) + ';')
        return out
    if op.startswith('link'):
        kind = {'linkParse':'DigitalLinkParseOptions', 'linkValidate':'DigitalLinkValidationOptions','linkNormalize':'DigitalLinkNormalizeOptions'}[op]
        out = ['gs1::'+kind+' o;']
        if 'primaryAi' in opts: out.append('o.primary_ai='+q(opts['primaryAi'])+';')
        out.append('o.unknown_query=gs1::UnknownQueryPolicy::'+('Reject' if opts.get('unknownQuery')=='reject' else 'Preserve')+';')
        if op == 'linkValidate': out.append('o.normalize='+str(opts.get('normalize',False)).lower()+';')
        if op == 'linkNormalize': out.append('o.mode='+q(opts.get('mode','specqr-deterministic'))+';')
        return out
    return []

f=json.load(open(sys.argv[1], encoding='utf-8'))
assert f['upstreamCommit']=='15ad15e5c770ea0e39072f8f88b2733018f02ffd'
names={'checkDigit':'calculate_check_digit','validateCheckDigit':'validate_check_digit','gtinDigit':'calculate_gtin_check_digit','gtinAppend':'append_gtin_check_digit','gtinValidate':'validate_gtin_check_digit','ssccDigit':'calculate_sscc_check_digit','ssccAppend':'append_sscc_check_digit','ssccValidate':'validate_sscc_check_digit','human':'parse_human_readable','raw':'parse_element_string','create':'create_element_string','validateElements':'validate_elements','validateRaw':'validate_element_string','linkCreate':'create_digital_link','linkParse':'parse_digital_link','linkValidate':'validate_digital_link','linkNormalize':'normalize_digital_link'}
# Differences are named explicitly after inspecting each upstream fixture. Each
# is checked as a rejection or a preserved payload, never silently skipped.
policy_deltas = {
    # Fail strictly before permissive replacement/unknown-query/primary-AI handling.
    'GS1_INVALID_PERCENT_ENCODING': {867, 874, 875, 876, 880, 883, 884, 885, 886, 889, 1319, 1320, 1321, 1322, 1323, 1324, 1325, 1326, 1327, 1334, 1335, 1336, 1352},
    # Only GS1 payload dot segments are rejected. Prefix/base dots are normalized.
    'GS1_INVALID_DIGITAL_LINK_PLACEMENT': {1155, 1157, 1374, 1376, 1377, 1378},
    # Full IDNA/UTS46 Unicode host mapping remains outside this portable URL contract.
    'GS1_DIGITAL_LINK_INVALID_URI': {1060, 1061, 1063, 1064, 1065, 1066, 1067, 1205, 1206, 1207, 1265, 1266, 1267},
}
strict_reject = {index: code for code, indices in policy_deltas.items() for index in indices}
assert len(strict_reject) == 42
output=['// Generated test data from MIT SpecQR, commit '+f['upstreamCommit']+'.', '// Execute with the helpers in gs1_tests.cpp.']
for index, case in enumerate(f['cases']):
    op=case['op']; exp=case['expected']; opts=case.get('options',{})
    lines=option_setup(op,opts)
    if op=='dictionary':
        lines += ['check(gs1::supported_ais().size()==50, "catalog count");']
        for i,ai in enumerate(exp):
            lines += ['check_info(gs1::supported_ais().at('+str(i)+'),'+q(ai['ai'])+','+q(ai['label'])+','+str(ai['length'].get('exact',0))+','+str(ai['length'].get('max',ai['length'].get('exact')))+','+str(ai['valueKind']=='text').lower()+','+q(ai['checkDigitRule'])+','+q(ai['digitalLinkRole'])+');']
    elif op=='info':
        lines += ['const auto* r=gs1::ai_info('+q(case['input'])+');']
        if exp is None: lines += ['check(r==nullptr,"unsupported AI");']
        else: lines += ['check(r!=nullptr,"known AI");', 'check_info(*r,'+q(exp['ai'])+','+q(exp['label'])+','+str(exp['length'].get('exact',0))+','+str(exp['length'].get('max',exp['length'].get('exact')))+','+str(exp['valueKind']=='text').lower()+','+q(exp['checkDigitRule'])+','+q(exp['digitalLinkRole'])+');']
    else:
        arg = es(case['elements']) if 'elements' in case else q(case['input'])
        call='gs1::'+names[op]+'('+arg+(',o' if lines else '')+')'
        if index in strict_reject:
            if op == 'linkValidate':
                lines += ['auto r='+call+';', 'check(!r.ok,\"intentional strict URL rejection\");', 'check(r.errors.size()==1 && r.errors[0].code=='+q(strict_reject[index])+',\"documented strict URL diagnostic\");']
            else:
                lines += ['expect_error([&]{ (void)'+call+'; });']
        elif isinstance(exp,dict) and 'throws' in exp:
            lines += ['expect_error([&]{ (void)'+call+'; });']
        elif op in ('checkDigit','gtinDigit','ssccDigit'):
            lines += ['check(std::string(1,'+call+')=='+q(exp)+',"digit");']
        elif isinstance(exp,bool): lines += ['check('+call+'=='+str(exp).lower()+',"boolean result");']
        elif isinstance(exp,str): lines += ['check('+call+'=='+q(exp)+',"string result");']
        elif op=='human': lines += ['check('+call+'=='+es(exp)+',"human elements");']
        elif op in ('raw','linkParse'):
            lines += ['auto r='+call+';']+parsed_checks(exp)
        else:
            lines += ['auto r='+call+';','check(r.ok=='+str(exp['ok']).lower()+',"validation status");']
            if exp['ok']:
                if 'result' in exp: lines += parsed_checks(exp['result'],'(*r.result)')
                else: lines += parsed_checks(exp)
                lines += ['check(r.warnings.size()=='+str(len(exp['warnings']))+',"warning count");']
                for j,e in enumerate(exp['warnings']): lines += ['check(r.warnings.at('+str(j)+').code=='+q(e['code'])+',"warning code");']
            else:
                lines += ['check(r.errors.size()=='+str(len(exp['errors']))+',"error count");']
                for j,e in enumerate(exp['errors']): lines += ['check(r.errors.at('+str(j)+').code=='+q(e['code'])+',"error code");']
    output.append('run_case('+q(str(index)+':'+op)+', [&] { '+' '.join(lines)+' });')
out=pathlib.Path(__file__).resolve().parents[1]/'gs1_fixture_cases.inc'
out.write_text('\n'.join(output)+'\n')
print('Generated',len(f['cases']),'GS1 fixtures;',len(strict_reject),'explicit policy/diagnostic differences')
