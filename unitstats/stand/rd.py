import struct, sys
sys.path.insert(0,'/var/www/dow_replay'); from rbf import keyname
def dump(path, root=None, flt=None):
    d=open(path,'rb').read()
    tabOff,tabCnt,chOff,chCnt,prOff,prCnt,blobOff,blobLen=struct.unpack_from('<8I',d,8)
    tables=[struct.unpack_from('<II',d,tabOff+i*8) for i in range(tabCnt)]
    ch=struct.unpack_from(f'<{chCnt}I',d,chOff); blob=d[blobOff:blobOff+blobLen]
    def bstr(o):
        n=struct.unpack_from('<I',blob,o)[0]; return blob[o+4:o+4+n].decode('latin-1')
    out=[]
    def walk(ti,pre,seen):
        if ti in seen: return
        seen=seen|{ti}
        c,s=tables[ti]
        if c==1: idx=[s]
        else: idx=ch[s:s+c]
        for pi in idx:
            t,k=struct.unpack_from('<HH',d,prOff+pi*8); v=struct.unpack_from('<I',d,prOff+pi*8+4)[0]
            n=keyname(k); p=pre+'/'+n
            if t==4 and v<tabCnt: walk(v,p,seen)
            elif t==1: out.append((p,round(struct.unpack('<f',struct.pack('<I',v))[0],4)))
            elif t==3: out.append((p,bstr(v)))
            elif t==2: out.append((p,struct.unpack('<i',struct.pack('<I',v))[0]))
            elif t==0: out.append((p,bool(v)))
            else: out.append((p,('t%d'%t,v)))
    walk(0,'',set())
    return out
if __name__=='__main__':
    for p,v in dump(sys.argv[1]):
        if len(sys.argv)<3 or any(f in p for f in sys.argv[2:]): print(p,'=',v)
