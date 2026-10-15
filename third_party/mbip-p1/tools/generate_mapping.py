import argparse,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
rows=json.loads((R/'schema/capability-mapping.json').read_text())['rows']
tokens=json.loads((R/'third_party/mvip/schema/capabilities.json').read_text())['tokens']
assert [r['source'] for r in rows]==tokens and len({r['target'] for r in rows})==len(tokens)
assert all(r['class'] in ['PASS_THROUGH','RENAMED','DERIVED','UNSUPPORTED','REJECTED'] for r in rows)
def cppname(t):return ''.join(x.title() for x in t.split('_'))
cpp='#pragma once\n#include "monaka/input/capabilities.hpp"\nnamespace monaka::bridge_input {\nenum class TranslationClass { PASS_THROUGH, RENAMED, DERIVED, UNSUPPORTED, REJECTED };\nstruct Mapping { monaka::input::Capability source; std::string_view target; TranslationClass classification; };\ninline constexpr std::array<Mapping,7> CapabilityMapping{{\n'+',\n'.join('    {monaka::input::Capability::'+cppname(r['source'])+',"'+r['target']+'",TranslationClass::'+r['class']+'}' for r in rows)+'\n}};\n}\n'
kt='package dev.monaka.bridgeinput\nimport dev.monaka.input.Capability\nenum class TranslationClass { PASS_THROUGH, RENAMED, DERIVED, UNSUPPORTED, REJECTED }\ndata class Mapping(val source: Capability, val target: String, val classification: TranslationClass)\ninternal val capabilityMapping = listOf(\n'+',\n'.join('    Mapping(Capability.'+r['source'].upper()+',"'+r['target']+'",TranslationClass.'+r['class']+')' for r in rows)+'\n)\n'
for path,data in [('cpp/include/monaka/bridge_input/mapping.hpp',cpp),('jvm/src/main/kotlin/dev/monaka/bridgeinput/Mapping.kt',kt)]:
 out=R/path
 if a.check:assert out.read_text(encoding='utf-8')==data,path
 else:out.parent.mkdir(parents=True,exist_ok=True);out.write_text(data,encoding='utf-8',newline='\n')
print('PASS seven explicit mapping rows / native-JVM drift')
