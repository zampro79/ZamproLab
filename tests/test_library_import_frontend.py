#!/usr/bin/env python3
"""Exercise the existing LAB and module-import flow in headless Firefox."""
import json, os, socket, subprocess, threading, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from frontend_asset_fixture import asset_bytes

ROOT=Path(__file__).resolve().parent.parent
model={'dirs':{'/'},'files':{},'puts':[],'runs':0,'fail':None,'corrupt':False}
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def reply(self,code,body=b'',kind='application/json',headers=None):
  if not isinstance(body,bytes):body=json.dumps(body).encode()
  self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)))
  for k,v in (headers or {}).items():self.send_header(k,v)
  self.end_headers()
  try:self.wfile.write(body)
  except (BrokenPipeError,ConnectionResetError):pass
 def do_GET(self):
  u=urlparse(self.path);path=u.path;q=parse_qs(u.query)
  if path=='/api/health':return self.reply(200,{'status':'ok','http':'running'})
  if path in ('/api/system','/api/memory','/api/wifi/status'):return self.reply(200,{})
  if path=='/api/program/status':return self.reply(200,{'state':'idle','last_result':None})
  if path=='/api/program/output':return self.reply(404,{})
  if path=='/api/files':
   p=q.get('path',['/'])[0]
   if p not in model['dirs']:return self.reply(404,{})
   pre='' if p=='/' else p
   entries=[{'name':x[len(pre)+1:],'type':'directory'} for x in model['dirs'] if x.startswith(pre+'/') and x!=pre+'/' and '/' not in x[len(pre)+1:]]
   entries += [{'name':x[len(pre)+1:],'type':'file','size':len(v)} for x,v in model['files'].items() if x.startswith(pre+'/') and '/' not in x[len(pre)+1:]]
   return self.reply(200,{'entries':entries,'path':p})
  if path=='/api/files/content':
   p=q['path'][0]
   if p not in model['files']:return self.reply(404,{})
   data=model['files'][p]
   if model['corrupt'] and model['puts']:data=b'corrupt'
   return self.reply(200,data,'application/octet-stream',{'ETag':'"test-etag"'})
  f=ROOT/('tests/library_import_frontend_mock.js' if path=='/mock-import.js' else 'web/'+('index.html' if path=='/' else path.lstrip('/')))
  if not f.is_file():return self.reply(404,{})
  return self.reply(200,asset_bytes(f),{'.html':'text/html','.js':'application/javascript','.css':'text/css'}.get(f.suffix,'text/plain'))
 def do_POST(self):
  p=urlparse(self.path).path;data=self.rfile.read(int(self.headers.get('Content-Length','0')))
  if p=='/api/files/mkdir':
   d=json.loads(data)['path'];model['dirs'].add(d);return self.reply(201,{'ok':True})
  if p.startswith('/api/program/'):model['runs']+=1
  return self.reply(404,{})
 def do_PUT(self):
  p=parse_qs(urlparse(self.path).query)['path'][0];data=self.rfile.read(int(self.headers['Content-Length']))
  if model['fail']=='network':self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
  if model['fail']:return self.reply(model['fail'],{'error':'injected'})
  if p in model['files'] and self.headers.get('If-None-Match')=='*':return self.reply(412,{})
  if p in model['files'] and self.headers.get('If-Match')!='"test-etag"':return self.reply(412,{})
  model['puts'].append({'path':p,'headers':dict(self.headers.items())});model['files'][p]=data;return self.reply(200 if len(model['puts'])>1 else 201,{'ok':True})

server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
proc=subprocess.Popen([os.environ.get('GECKODRIVER','geckodriver'),'--port',str(port)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);session=None
def call(method,path,data=None):
 q=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=None if data is None else json.dumps(data).encode(),method=method,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(q,timeout=20) as r:return json.load(r)['value']
def js(code,*args):return call('POST',session+'/execute/sync',{'script':code,'args':args})
def wait(code,timeout=12):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if js(code):return
  time.sleep(.1)
 raise AssertionError('timeout: '+code)
def choose(name,data,dest='/lib/utils.lua',confirmations=(True,)):
 js('window.__confirmations=arguments[0];window.__dialogMessages=[];window.prompt=(m,d)=>{window.__dialogMessages.push(m);return arguments[1]};window.confirm=m=>{window.__dialogMessages.push(m);return window.__confirmations.shift()??false};let d=new DataTransfer();d.items.add(new File([arguments[2]],arguments[3]));let e=document.querySelector("#import-library-input");e.files=d.files;e.dispatchEvent(new Event("change",{bubbles:true}));',list(confirmations),dest,data,name)
try:
 for _ in range(100):
  try:call('GET','/status');break
  except Exception:time.sleep(.1)
 sid=call('POST','/session',{'capabilities':{'alwaysMatch':{'browserName':'firefox','moz:firefoxOptions':{'args':['-headless']}}}})['sessionId'];session='/session/'+sid
 call('POST',session+'/url',{'url':f'http://127.0.0.1:{server.server_port}/'})
 wait('return !!window.ZamLibraryImport && !!document.querySelector("#import-library")')
 assert js('return document.querySelector("#import-library").textContent')=='Import Library'
 assert '**Import Library**' in (ROOT/'docs/lua-userfs-modules.md').read_text()
 assert 'Importa libreria' not in (ROOT/'web/index.html').read_text()+(ROOT/'docs/lua-userfs-modules.md').read_text()
 choose('utils.lua','return 42');wait('return document.querySelector("#file-status").textContent.includes("Imported /lib/utils.lua")')
 assert js('return window.__dialogMessages')[0].startswith('Destination for ')
 assert 'Use require("lib.utils")' in js('return window.__dialogMessages')[1]
 assert model['files']['/lib/utils.lua']==b'return 42' and model['puts'][0]['headers'].get('If-None-Match')=='*'
 assert 'require("lib.utils")' in js('return document.querySelector("#file-status").textContent')
 choose('utils.lua','new',confirmations=(True,False));wait('return document.querySelector("#file-status").textContent.includes("cancelled")');assert model['files']['/lib/utils.lua']==b'return 42'
 choose('utils.lua','new',confirmations=(True,True));wait('return document.querySelector("#file-status").textContent.includes("Imported")');assert model['files']['/lib/utils.lua']==b'new' and model['puts'][-1]['headers'].get('If-Match')=='"test-etag"'
 for name,data,dest,expected in [('x.txt','x','/lib/x.lua','Choose a .lua'),('bad-name.lua','x','/lib/x.lua','Invalid module'),('x.lua','x','/lib/../x.lua','Invalid destination'),('x.lua','x'*32769,'/lib/x.lua','32 KiB')]:
  choose(name,data,dest);wait('return document.querySelector("#file-status").textContent.includes('+json.dumps(expected)+')')
 for mode,expected in [(507,'Not enough space'),(500,'Device error'),('network','outcome unconfirmed')]:
  model['fail']=mode;choose('x.lua','x','/lib/x.lua');wait('return document.querySelector("#file-status").textContent.includes('+json.dumps(expected)+')');model['fail']=None
 model['corrupt']=True;choose('x.lua','x','/lib/x.lua');wait('return document.querySelector("#file-status").textContent.includes("verification failed")');model['corrupt']=False
 assert model['runs']==0
 js('let s=document.createElement("script");s.src="/mock-import.js";document.head.append(s)')
 wait('return typeof window.__importTest==="string"');result=js('return window.__importTest');assert result=='PASS',result
 print('Firefox library import: UI, validation, ETag, errors, readback and no RUN PASS')
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
