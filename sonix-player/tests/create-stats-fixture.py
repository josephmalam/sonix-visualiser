"""Small tagged FLAC/PNG library for the isolated Stats integration test."""
import ctypes as C
from pathlib import Path
import struct
import sys
import zlib

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=True)
class Info(C.Structure):
    _fields_ = [('frames', C.c_int64), ('samplerate', C.c_int), ('channels', C.c_int),
                ('format', C.c_int), ('sections', C.c_int), ('seekable', C.c_int)]
lib = C.CDLL('libsndfile.so.1')
lib.sf_open.argtypes = [C.c_char_p, C.c_int, C.POINTER(Info)]; lib.sf_open.restype = C.c_void_p
lib.sf_set_string.argtypes = [C.c_void_p,C.c_int,C.c_char_p]
lib.sf_writef_short.argtypes = [C.c_void_p,C.POINTER(C.c_int16),C.c_int64]
lib.sf_close.argtypes = [C.c_void_p]
def chunk(kind, data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
for i in range(24):
    folder = root / f'album-{i:02}'; folder.mkdir(exist_ok=True)
    info = Info(samplerate=44100, channels=1, format=0x170002)
    out = lib.sf_open(str(folder/'01.flac').encode(), 0x20, C.byref(info)); assert out
    for key,value in [(1,['Atmosphere','Blue Monday','Ceremony','Dreams Never End'][i] if i<4 else 'Song'),(4,'Joy Division' if i==0 else 'New Order' if i<4 else f'Track Artist {i:02}'),(7,'Shared title' if i<2 else f'Album {i:02}')]:
        assert lib.sf_set_string(out,key,value.encode()) == 0
    samples = (C.c_int16 * 4410)()
    assert lib.sf_writef_short(out,samples,4410) == 4410
    assert lib.sf_close(out) == 0
    file = folder/'01.flac'
    raw = file.read_bytes(); blocks = bytearray(raw[:4]); at = 4
    while at < len(raw):
        header = raw[at]; size = int.from_bytes(raw[at+1:at+4], 'big')
        payload = raw[at+4:at+4+size]; at += 4+size
        if header & 127 == 4:
            vendor = 4+int.from_bytes(payload[:4], 'little')
            count = int.from_bytes(payload[vendor:vendor+4], 'little')
            tag = f'ALBUMARTIST=Album Artist {i:02}'.encode()
            payload = payload[:vendor]+(count+1).to_bytes(4,'little')+payload[vendor+4:]+len(tag).to_bytes(4,'little')+tag
        blocks += bytes([header])+len(payload).to_bytes(3,'big')+payload
        if header & 128:
            blocks += raw[at:]; break
    file.write_bytes(blocks)
    color = bytes((30+i*73%200,30+i*47%200,30+i*31%200))
    rows = b''.join(b'\0'+b''.join(color if (x//8+y//8)%2 else bytes(v//2 for v in color) for x in range(64)) for y in range(64))
    png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,64,8,2,0,0,0))
    png += chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
    (folder/'cover.png').write_bytes(png)
