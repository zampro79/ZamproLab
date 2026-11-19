#!/usr/bin/env python3
"""Real host sockets around the bounded reader; this is not an IDF test."""
import concurrent.futures,ctypes,os,signal,socket,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
signal.signal(signal.SIGPIPE,signal.SIG_IGN)
with tempfile.TemporaryDirectory() as d:
 lib=Path(d)/'stream.so'
 subprocess.run(['cc','-std=c11','-shared','-fPIC','-I'+str(ROOT/'components/zam_http'),str(ROOT/'components/zam_http/zam_web_stream.c'),'-o',str(lib)],check=True)
 dll=ctypes.CDLL(str(lib));libc=ctypes.CDLL(None);libc.fopen.argtypes=[ctypes.c_char_p,ctypes.c_char_p];libc.fopen.restype=ctypes.c_void_p;libc.fclose.argtypes=[ctypes.c_void_p]
 SEND=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t)
 dll.zam_web_stream.argtypes=[ctypes.c_void_p,ctypes.c_size_t,SEND,ctypes.c_void_p];dll.zam_web_stream.restype=ctypes.c_int
 def case(seed,disconnect=False,slow=False,timeout=False):
  payload=bytes((i+seed)%256 for i in range(65536));path=Path(d)/('data'+str(seed));path.write_bytes(payload);a,b=socket.socketpair();a.settimeout(.05 if timeout else 2);b.settimeout(2)
  if timeout:a.setsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF,1024)
  def producer():
   f=libc.fopen(os.fsencode(path),b'rb');assert f
   @SEND
   def send(ctx,ptr,n):
    try:a.sendall(ctypes.string_at(ptr,n));return 0
    except OSError as exc:print("send failed",repr(exc));return -1
   result=dll.zam_web_stream(f,len(payload),send,None);assert libc.fclose(f)==0;a.close();return result
  with concurrent.futures.ThreadPoolExecutor(max_workers=1) as e:
   future=e.submit(producer);data=bytearray()
   while not timeout:
    chunk=b.recv(1024)
    if not chunk:break
    data.extend(chunk)
    if disconnect:b.close();break
    if slow:time.sleep(.001)
   if not disconnect and not timeout:b.close()
   result=future.result(timeout=5)
   if timeout:b.close()
  if disconnect or timeout:assert result!=0
  else:assert result==0 and data==payload,(seed,result,len(data),len(payload))
 for count in (2,4):
  with concurrent.futures.ThreadPoolExecutor(max_workers=count) as e:list(e.map(case,range(count)))
 case(11,slow=True);case(12,disconnect=True);case(13,timeout=True)
 print('Host sockets: 2/4 clients, byte equality, slow client, timeout and disconnect PASS')
