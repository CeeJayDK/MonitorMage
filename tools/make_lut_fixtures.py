"""Independent numerical LUT fixtures, using only Python's standard library.
No photographic image processing or colour management. Each pixel is test data.
"""
from pathlib import Path
import struct,zlib,math
ROOT=Path(__file__).resolve().parents[1]/'tests/data/lut-images'
ROOT.mkdir(parents=True,exist_ok=True)
def png(name,w,h,pixels,bits=8,alpha=False):
    channels=4 if alpha else 3
    pack=(lambda v:bytes(v)) if bits==8 else (lambda v:struct.pack('>'+str(len(v))+'H',*v))
    data=b''.join(b'\x00'+pack([v for pixel in pixels[y*w:(y+1)*w] for v in pixel]) for y in range(h))
    def chunk(kind,value):return struct.pack('>I',len(value))+kind+value+struct.pack('>I',zlib.crc32(kind+value)&0xffffffff)
    raw=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,bits,6 if alpha else 2,0,0,0))
    raw+=chunk(b'gAMA',struct.pack('>I',100000))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
    (ROOT/name).write_bytes(raw)
def tiff(name,w,h,pixels,bits=16,floating=False):
    fmt='f' if floating else 'H' if bits==16 else 'B'
    data=struct.pack('<'+str(w*h*3)+fmt,*[v for p in pixels for v in p])
    tags=[(256,4,1,w),(257,4,1,h),(258,3,3,None),(259,3,1,1),(262,3,1,2),(273,4,1,None),(277,3,1,3),(278,4,1,h),(279,4,1,len(data)),(284,3,1,1),(339,3,3,None)]
    offset=8+2+len(tags)*12+4;bits_at=offset;sample_at=offset+6;pixels_at=offset+12
    output=b'II'+struct.pack('<HI',42,8)+struct.pack('<H',len(tags))
    for tag,kind,count,value in tags:
        if tag==258:value=bits_at
        if tag==339:value=sample_at
        if tag==273:value=pixels_at
        output+=struct.pack('<HHII',tag,kind,count,value)
    output+=struct.pack('<I',0)+struct.pack('<3H',bits,bits,bits)+struct.pack('<3H',*([3 if floating else 1]*3))+data
    (ROOT/name).write_bytes(output)
ramp8=[(i*15,i*14,i*12) for i in range(17)]
png('horizontal-8.png',17,1,ramp8)
png('vertical-8.png',1,17,ramp8)
png('repeated-rows-8.png',17,4,ramp8*4)
png('transparent.png',17,1,[(*v,128) for v in ramp8],alpha=True)
ramp16=[(round((i/256)*65535),round((i/256)**1.1*62000),round((i/256)**1.2*60000)) for i in range(257)]
png('horizontal-16.png',257,1,ramp16,16)
tiff('horizontal-16.tiff',257,1,ramp16)
# A valid two-page TIFF referencing the same pixel data in both IFDs.
raw=bytearray((ROOT/'horizontal-16.tiff').read_bytes());ifd_length=2+11*12+4
second=bytes(raw[8:8+ifd_length]);struct.pack_into('<I',raw,8+2+11*12,len(raw))
(ROOT/'multipage.tiff').write_bytes(bytes(raw)+second)
tiff('float-32.tiff',17,1,[(i/16,(i/16)**1.1,(i/16)**1.2) for i in range(17)],32,True)
tiff('out-of-range-float.tiff',2,1,[(0.,0.,0.),(1.2,1.,1.)],32,True)
n=4
cube=[(round(r/(n-1)*255),round(g/(n-1)*230),round(b/(n-1)*204)) for b in range(n) for g in range(n) for r in range(n)]
reshade=[cube[r+g*n+b*n*n] for g in range(n) for b in range(n) for r in range(n)]
png('reshade-separable.png',n*n,n,reshade)
png('hald-separable.png',8,8,cube)
coupled=[(min(255,p[0]+g*12),p[1],p[2]) for g in range(n) for b in range(n) for r in range(n) for p in [cube[r+g*n+b*n*n]]]
png('reshade-coupled.png',n*n,n,coupled)
png('not-a-lut.png',7,5,[((i*71)%256,(i*39)%256,(i*17)%256) for i in range(35)])
print('Created independent 8/16-bit PNG, 16/32-bit TIFF, vertical/repeated ramps and separable/coupled 3D-layout fixtures.')
