#!/usr/bin/env python3
"""Exercise the existing LAB and module-import flow in headless Firefox."""
import hashlib, json, os, socket, subprocess, threading, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from frontend_asset_fixture import asset_bytes

ROOT=Path(__file__).resolve().parent.parent
model={'dirs':{'/'},'files':{},'puts':[],'runs':0,'fail':None,'corrupt':False,'delete_mode':None,'list_mode':None,'etag_change':False,'gets':0,'deletes':[],'delay_file':False}
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
  if model.get('race') and p=='/lib/other.lua':model['files'][p]=b'concurrent update'
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
def wait_for_library_target(selector,timeout=12):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if js("""const e=document.querySelector(arguments[0]);const editor=document.querySelector("#editor-text");
   return document.querySelector("#current-dir").textContent==="Installed libraries: /lib" &&
    !editor.disabled && !!e && !e.disabled && e.getClientRects().length>0;""",selector):return
  time.sleep(.1)
 raise AssertionError('Library Manager target not ready: '+selector)
def choose(name,data,dest='/lib/utils.lua',confirmations=(True,)):
 js('window.__confirmations=arguments[0];window.__dialogMessages=[];window.prompt=(m,d)=>{window.__dialogMessages.push(m);return arguments[1]};window.confirm=m=>{window.__dialogMessages.push(m);return window.__confirmations.shift()??false};let d=new DataTransfer();d.items.add(new File([arguments[2]],arguments[3]));let e=document.querySelector("#import-library-input");e.files=d.files;e.dispatchEvent(new Event("change",{bubbles:true}));',list(confirmations),dest,data,name)
try:
 for _ in range(100):
  try:call('GET','/status');break
  except Exception:time.sleep(.1)
 sid=call('POST','/session',{'capabilities':{'alwaysMatch':{'browserName':'firefox','moz:firefoxOptions':{'args':['-headless']}}}})['sessionId'];session='/session/'+sid
 call('POST',session+'/url',{'url':f'http://127.0.0.1:{server.server_port}/'})
 wait('return !!window.ZamLibraryImport && !!document.querySelector("#show-libraries")')
 js('window.__scriptErrors=[];window.addEventListener("error",e=>window.__scriptErrors.push(String(e.message)));window.addEventListener("unhandledrejection",e=>window.__scriptErrors.push(String(e.reason)));document.querySelector(`[data-view="system"]`).click()');wait('return !document.querySelector("#system").hidden')
 js('document.querySelector(`[data-view="settings"]`).click()');wait('return !document.querySelector("#settings").hidden && !!document.querySelector("#net-scan")')
 js('document.querySelector(`[data-view="lab"]`).click()');wait('return !document.querySelector("#lab").hidden')
 js('document.querySelector("#show-libraries").click()');wait('return document.querySelector("#file-list").textContent.includes("No installed libraries")')
 model['dirs'].add('/lib');model['files']['/lib/only.lua']=b'return 1';js('document.querySelector("#refresh-files").click()');wait('return document.querySelector("#file-list").textContent.includes("lib.only")');del model['files']['/lib/only.lua']
 model['dirs'].update(['/lib','/lib/sensors']);model['files'].update({'/lib/utils.lua':b'return 1','/lib/other.lua':b'return 2','/lib/bad-name.lua':b'x','/lib/readme.txt':b'x','/lib/bytecode.lua':b'\x1bLua000','/lib/huge.lua':b'x'*32769,'/lib/sensors/bme280.lua':b'return 3','/unrelated.lua':b'keep'})
 js('document.querySelector("#refresh-files").click()');wait('return document.querySelector("#file-list").textContent.includes("lib.utils")')
 t=js('return document.querySelector("#file-list").textContent');assert 'lib.other' in t and 'bad-name' not in t and 'readme.txt' not in t and 'bytecode.lua' not in t and 'huge.lua' not in t and 'sensors' in t and 'unrelated.lua' not in t
 model['delay_file']=True;js('document.querySelector("#refresh-files").click()');time.sleep(.15);js('document.querySelector("#show-user-files").click()');wait('return document.querySelector("#file-list").textContent.includes("unrelated.lua")');time.sleep(.8);assert 'unrelated.lua' in js('return document.querySelector("#file-list").textContent');model['delay_file']=False
 js('document.querySelector("#show-libraries").click()');wait('return document.querySelector("#file-list").textContent.includes("lib.utils")')
 js('document.querySelector(`[aria-label="Open sensors"]`).click()');wait('return document.querySelector("#file-list").textContent.includes("lib.sensors.bme280")')
 js('document.querySelector("#parent-dir").click()');wait('return document.querySelector("#file-list").textContent.includes("lib.utils")')
 js('document.querySelector(`[aria-label="Open utils.lua"]`).click()');wait('return document.querySelector("#editor-text").value==="return 1"')
 target='[aria-label="Open other.lua"]';wait_for_library_target(target)
 try:wait_for_library_target('[aria-label="Open nonexistent diagnostic target"]',timeout=.3)
 except AssertionError as e:assert 'Library Manager target not ready:' in str(e)
 else:raise AssertionError('Missing target unexpectedly became ready')
 wait_for_library_target(target)
 js('window.confirm=()=>false;let e=document.querySelector("#editor-text");e.value="unsaved";e.dispatchEvent(new Event("input",{bubbles:true}));document.querySelector(`[aria-label="Open other.lua"]`).click()');time.sleep(.2);assert js('return document.querySelector("#editor-text").value')=='unsaved'
 js('document.querySelector(`[aria-label="Delete lib.utils"]`).click()');wait('return !document.querySelector("#editor-text").disabled');assert '/lib/utils.lua' in model['files'] and not model['deletes']
 js('window.__answers=[true,false];window.confirm=()=>window.__answers.shift();document.querySelector(`[aria-label="Delete lib.utils"]`).click()');time.sleep(.4);assert '/lib/utils.lua' in model['files'] and not model['deletes']
 js('window.__answers=[true,true];window.confirm=()=>window.__answers.shift();document.querySelector(`[aria-label="Delete lib.utils"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("Deleted: lib.utils")');assert '/lib/utils.lua' not in model['files'] and model['files']['/unrelated.lua']==b'keep' and not js('return !!document.querySelector(`[aria-label="Delete lib.utils"]`)')
 model['files']['/lib/utils.lua']=b'return 1';js('document.querySelector("#refresh-files").click()');wait('return !!document.querySelector(`[aria-label="Delete lib.utils"]`)')
 model['etag_change']=True;model['gets']=0;js('window.confirm=()=>true;document.querySelector(`[aria-label="Delete lib.utils"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("File changed")');assert model['files']['/lib/utils.lua']==b'changed concurrently' and model['deletes'].count('/lib/utils.lua')==1;model['etag_change']=False
 del model['files']['/lib/other.lua'];js('document.querySelector(`[aria-label="Delete lib.other"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("File not found")');model['files']['/lib/other.lua']=b'return 2'
 model['race']=True;js('window.confirm=()=>true;document.querySelector(`[aria-label="Delete lib.other"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("File changed")');assert model['files']['/lib/other.lua']==b'concurrent update';model['race']=False
 model['delete_mode']=403;js('document.querySelector(`[aria-label="Delete lib.other"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("Operation not permitted")');assert '/lib/other.lua' in model['files']
 model['delete_mode']='network';js('document.querySelector(`[aria-label="Delete lib.other"]`).click()');wait('return document.querySelector("#file-status").textContent.includes("Connection unavailable")');assert '/lib/other.lua' in model['files'];model['delete_mode']=None
 model['list_mode']=500;js('document.querySelector("#refresh-files").click()');wait('return document.querySelector("#file-status").textContent.includes("List unavailable")');assert js('return !!document.querySelector(`[aria-label="Delete lib.other"]`)');model['list_mode']=None
 choose('fresh.lua','return 44','/lib/fresh.lua');wait('return document.querySelector("#file-status").textContent.includes("Imported /lib/fresh.lua")');assert model['files']['/lib/fresh.lua']==b'return 44' and 'lib.fresh' in js('return document.querySelector("#file-list").textContent')
 assert js('return window.__scriptErrors')==[]
 assert model['runs']==0 and model['files']['/unrelated.lua']==b'keep' and '/lib/sensors/bme280.lua' in model['files']
 print('Firefox Libraries: browse, nested, import, dirty, ETag recheck, delete, errors, isolation and no RUN PASS')
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
