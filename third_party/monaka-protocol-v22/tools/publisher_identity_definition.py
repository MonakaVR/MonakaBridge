"""C2.2: six new namespace messages; frozen C2.1 definitions are unchanged.

The public Common World models expose nullable source_publisher_id. Null means
legacy, never a trusted identity. A present value selects the new message family.
"""
import copy,json,hashlib
from authority_definition import MESSAGES
COMMON={n:t for n,(p,t) in MESSAGES.items() if p=='monaka.common_world'}
PROTOCOL='monaka.common_world_identity'

def define(defs,models,put,root):
    for n in COMMON:
        d=copy.deepcopy(defs[n]);d['properties']['protocol']['const']=PROTOCOL
        d['properties']['source_publisher_id']={'$ref':'#/$defs/Id'}
        d['required'].append('source_publisher_id')
        d['properties']['version']['properties']['minor']={'type':'integer','const':2}
        defs['PublisherBound'+n]=d
        # Additive API field; legacy schema/namespace does not gain a required field.
        models[n].append(('source_publisher_id','Id',True,True))
    schema={'$schema':'https://json-schema.org/draft/2020-12/schema','$id':'https://monaka.dev/schema/v2.2/common-world-publisher-identity.schema.json','$comment':'C2.2 SHA256 '+hashlib.sha256((root/'docs/C2.2.md').read_bytes()).hexdigest(),'oneOf':[{'$ref':'#/$defs/PublisherBound'+n} for n in COMMON],'$defs':defs}
    put('schema/common-world-publisher-identity.schema.json',json.dumps(schema,indent=2)+'\n')

def cpp_codec(text):
    dispatch='\n'.join(f'    if(p=="{PROTOCOL}" && t=="{t}") {{ need(j["version"].contains("minor"),ErrorCode::MissingField,"version.minor"); need(j["version"]["minor"].is_number(),ErrorCode::InvalidType,"version.minor"); need(j["version"]["minor"]==2,ErrorCode::UnsupportedVersion,"publisher identity requires wire 2.2"); return "PublisherBound{n}"; }}' for n,t in COMMON.items())
    text=text.replace('    fail(ErrorCode::UnsupportedMessage,"protocol/type");',dispatch+'\n    fail(ErrorCode::UnsupportedMessage,"protocol/type");')
    text=text.replace('    nonblank(j.at("publisher_id"));',f'    nonblank(j.at("publisher_id"));\n    if(j["protocol"]=="{PROTOCOL}") nonblank(j.at("source_publisher_id"));')
    text=text.replace('j["protocol"]=="monaka.common_world") { authoritySemantics',f'j["protocol"]=="monaka.common_world" || j["protocol"]=="{PROTOCOL}") {{ authoritySemantics')
    for n in COMMON:text=text.replace(f'else if(name=="{n}")',f'else if(name=="{n}" || name=="PublisherBound{n}")')
    text=text.replace('j["version"]["minor"]=(j["protocol"]',f'j["version"]["minor"]=j["protocol"]=="{PROTOCOL}"?2:(j["protocol"]')
    return text

def kt_codec(text):
    dispatch='\n'.join(f'        "{PROTOCOL}" to "{t}" -> {{ need(ver.has("minor"),ErrorCode.MissingField,"version.minor"); need(ver["minor"].number(),ErrorCode.InvalidType,"version.minor"); need(ver["minor"].asDouble==2.0,ErrorCode.UnsupportedVersion,"publisher identity requires wire 2.2"); "PublisherBound{n}" }}' for n,t in COMMON.items())
    text=text.replace('        else -> fail(ErrorCode.UnsupportedMessage,"protocol/type")',dispatch+'\n        else -> fail(ErrorCode.UnsupportedMessage,"protocol/type")',1)
    text=text.replace('    nonblank(j["publisher_id"])',f'    nonblank(j["publisher_id"])\n    if(j["protocol"].asString=="{PROTOCOL}") nonblank(j["source_publisher_id"])')
    text=text.replace('listOf("monaka.hmd_authority","monaka.common_world")) { authoritySemantics',f'listOf("monaka.hmd_authority","monaka.common_world","{PROTOCOL}")) {{ authoritySemantics')
    for n in COMMON:text=text.replace(f'"{n}" -> read{n}(j)',f'"{n}", "PublisherBound{n}" -> read{n}(j)')
    text=text.replace('addProperty("minor",if(',f'addProperty("minor",if(j["protocol"].asString=="{PROTOCOL}") 2 else if(')
    return text
