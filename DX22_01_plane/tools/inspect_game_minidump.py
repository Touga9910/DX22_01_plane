"""Read exception context and symbolize stack candidates (not an unwind)."""
import json
from pathlib import Path
import struct
import subprocess
import argparse

def inspect(dump,exe):
    data=Path(dump).read_bytes()
    count,directory=struct.unpack_from('<II',data,8)
    streams={}
    for index in range(count):
        kind,size,rva=struct.unpack_from('<III',data,directory+12*index)
        streams[kind]=(rva,size)
    exception=streams[6][0]
    thread=struct.unpack_from('<I',data,exception)[0]
    _,context=struct.unpack_from('<II',data,exception+160)
    registers={name:struct.unpack_from('<Q',data,context+offset)[0] for name,offset in
               [('rcx',128),('rsp',152),('rbp',160),('rip',248)]}
    modules=streams[4][0]
    for index in range(struct.unpack_from('<I',data,modules)[0]):
        offset=modules+4+108*index
        base,size=struct.unpack_from('<QI',data,offset)
        name_rva=struct.unpack_from('<I',data,offset+20)[0]
        length=struct.unpack_from('<I',data,name_rva)[0]
        name=data[name_rva+4:name_rva+4+length].decode('utf-16-le')
        if name.endswith('DX22_01_plane.exe'):break
    threads=streams[3][0]
    for index in range(struct.unpack_from('<I',data,threads)[0]):
        offset=threads+4+48*index
        if struct.unpack_from('<I',data,offset)[0]==thread:
            start,stack_size,stack_rva=struct.unpack_from('<QII',data,offset+24)
            break
    candidates=[]
    for offset in range(max(0,registers['rsp']-start),min(stack_size,registers['rsp']-start+8192),8):
        value=struct.unpack_from('<Q',data,stack_rva+offset)[0]
        if base<=value<base+size:candidates.append((hex(start+offset),hex(value-base)))
    addresses=[hex(registers['rip']-base)]+[address for _,address in candidates]
    symbolizer='C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin/llvm-symbolizer.exe'
    result=subprocess.run([symbolizer,'--obj='+str(Path(exe).resolve()),'--relative-address','--output-style=JSON'],
                          input='\n'.join(addresses),text=True,encoding='utf-8',capture_output=True,check=True)
    return dict(registers=registers,module_base=base,stack_candidates=candidates,
                symbols=[json.loads(line) for line in result.stdout.splitlines() if line.strip()])

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('dump');parser.add_argument('exe');parser.add_argument('output')
    args=parser.parse_args()
    result=inspect(args.dump,args.exe)
    Path(args.output).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(registers=result['registers'],symbolized_candidates=len(result['symbols']),output=args.output),ensure_ascii=True))
