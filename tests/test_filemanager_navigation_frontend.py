#!/usr/bin/env python3
"""Exercise the existing LAB and module-import flow in headless Firefox."""
import hashlib, json, os, socket, subprocess, threading, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from frontend_asset_fixture import asset_bytes

ROOT=Path(__file__).resolve().parent.parent
model={'dirs':{'/'},'files':{},'puts':[],'runs':0,'fail':None,'corrupt':False,'delete_mode':None,'list_mode':None,'etag_change':False,'gets':0,'deletes':[],'delay_file':False,'requests':[],'delay_lib':False}
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
   p=q.get('path',['/'])[0];model['requests'].append(p)
   if model['delay_lib'] and p=='/lib':time.sleep(1.5)
   if model.get('truncated') and p=='/':return self.reply(200,b'{\"entries\":[',kind='application/json')
   if model['list_mode'] and p=='/lib':return self.reply(model['list_mode'],{'error':'list failed'})
   if p not in model['dirs']:return self.reply(404,{})
   pre='' if p=='/' else p
   entries=[{'name':x[len(pre)+1:],'type':'directory'} for x in model['dirs'] if x.startswith(pre+'/') and x!=pre+'/' and '/' not in x[len(pre)+1:]]
   entries += [{'name':x[len(pre)+1:],'type':'file','size':len(v)} for x,v in model['files'].items() if x.startswith(pre+'/') and '/' not in x[len(pre)+1:]]
   return self.reply(200,{'entries':entries,'path':p})
  if path=='/api/files/content':
   p=q['path'][0]
   if model['delay_file'] and p=='/lib/other.lua':time.sleep(.7)
   if p not in model['files']:return self.reply(404,{})
   data=model['files'][p]
   if model['etag_change'] and p=='/lib/utils.lua':
    model['gets']+=1
    if model['gets']==2:model['files'][p]=data=b'changed concurrently'
   if model['corrupt'] and model['puts']:data=b'corrupt'
   return self.reply(200,data,'application/octet-stream',{'ETag':'"'+hashlib.sha256(data).hexdigest()+'"'})
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
  if p in model['files'] and self.headers.get('If-Match')!='"'+hashlib.sha256(model['files'][p]).hexdigest()+'"':return self.reply(412,{})
  model['puts'].append({'path':p,'headers':dict(self.headers.items())});model['files'][p]=data;return self.reply(200 if len(model['puts'])>1 else 201,{'ok':True})

 def do_DELETE(self):
  p=parse_qs(urlparse(self.path).query)['path'][0];model['deletes'].append(p)
  if model['delete_mode']=='network':self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
  if model['delete_mode']:return self.reply(model['delete_mode'],{'error':'failed'})
  if p not in model['files']:return self.reply(404,{'error':'missing'})
  if model.get('race_delete'):model['files'][p]=b'new version'
  tag=self.headers.get('If-Match')
  if not tag:return self.reply(428,{'error':'precondition required'})
  if tag!='"'+hashlib.sha256(model['files'][p]).hexdigest()+'"':return self.reply(412,{'error':'changed'})
  del model['files'][p];return self.reply(200,{'ok':True})

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
 model['files']['/root.lua']=b'return 1'
 js('document.querySelector("#refresh-files").click()')
 wait('return !!document.querySelector(`[aria-label="Open root.lua"]`)')
 def click(sel):js('document.querySelector(arguments[0]).click()',sel)
 def root():
  wait('return document.querySelector("#current-dir").textContent==="/" && !!document.querySelector(`[aria-label="Open root.lua"]`)')
  assert model['requests'][-1]=='/' and 'List unavailable' not in js('return document.querySelector("#file-status").textContent')
 # Real defect: Samples -> missing Libraries -> UserFS must use root.
 model['files']['/root.lua']=b'return 1'
 click('#show-samples');click('#show-libraries');wait('return document.querySelector("#file-list").textContent.includes("Library directory not found")')
 click('#show-user-files');root()
 click('#refresh-files');root()
 # Existing empty and populated library, nested navigation and parent.
 model['dirs'].add('/lib');click('#show-libraries');wait('return document.querySelector("#file-list").textContent.includes("No installed libraries")');assert 'directory not found' not in js('return document.querySelector("#file-list").textContent')
 click('#show-user-files');root()
 model['dirs'].add('/lib/nested');model['files']['/lib/nested/a.lua']=b'return 2'
 click('#show-libraries');wait('return !!document.querySelector(`[aria-label="Open nested"]`)');click('[aria-label="Open nested"]');wait('return document.querySelector("#current-dir").textContent.includes("/lib/nested")')
 click('#show-user-files');root()
 click('#show-libraries');wait('return document.querySelector("#current-dir").textContent.includes("/lib")');click('#show-samples');click('#show-user-files');root()
 # An older library response cannot replace a newer root view.
 model['delay_lib']=True;click('#show-libraries');time.sleep(.1);click('#show-user-files');root();time.sleep(1.8);root();model['delay_lib']=False
 # Samples rendering is synchronous; a pending library response must not replace it or later root.
 model['delay_lib']=True;click('#show-libraries');time.sleep(.1);click('#show-samples');click('#show-user-files');root();time.sleep(1.8);root();model['delay_lib']=False
 # Ordinary UserFS deletion: stale ETag and explicit 428 must not trigger retries.
 model['race_delete']=True;js('window.confirm=()=>true;document.querySelector(`[aria-label="Delete root.lua"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("File changed")');assert model['files']['/root.lua']==b'new version';assert model['deletes'].count('/root.lua')==1;model['race_delete']=False
 model['delete_mode']=428;js('document.querySelector(`[aria-label="Delete root.lua"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("Delete failed")');assert model['files']['/root.lua']==b'new version';assert model['deletes'].count('/root.lua')==2;model['delete_mode']=None
 click('#refresh-files');root()
 model['truncated']=True;click('#refresh-files');wait('return document.querySelector("#file-status").textContent.includes("List unavailable")');assert js('return !!document.querySelector(`[aria-label="Open root.lua"]`)');model['truncated']=False;click('#refresh-files');root()
 print('Firefox navigation: missing/empty/nested libraries, samples, refresh and delayed responses PASS')
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
