#!/usr/bin/env python3
"""Browser regression with local mocked HTTP; requires Firefox and geckodriver."""
import json
import base64
import os
from pathlib import Path
import socket
import subprocess
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

from frontend_asset_fixture import asset_bytes

ROOT=Path(__file__).resolve().parent.parent
model={'available':False,'state':'idle','result':None,'runs':[],'stops':0,'files':{'/hello.lua':'print("hello")\n','/other.lua':'print("other")\n'},'etag':{'/hello.lua':'"v1"','/other.lua':'"v1"'},'conflict':False,'run_mode':'running','stop_disconnect':False,'received':[],'transport':[],'ids':{},'delay_save':False,'network_down':False,'status_mode':'normal','save_error':False}
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def respond(self,code,body=b'',kind='application/json',headers=None):
  if not isinstance(body,bytes):body=json.dumps(body).encode()
  self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)))
  for k,v in (headers or {}).items():self.send_header(k,v)
  self.end_headers()
  try:self.wfile.write(body)
  except (BrokenPipeError,ConnectionResetError):pass
 def do_GET(self):
  path=urlparse(self.path).path
  if path=='/api/program/output':
   model['requests']=model.get('requests',0)+1
   if not model.get('out_available',False):return self.respond(404,{})
   if model.get('fail_output',False):return self.respond(503,{})
   q=parse_qs(urlparse(self.path).query);cursor=int(q['cursor'][0]);boot=q['boot_id'][0];run=q['execution_id'][0];data=model.get('output',b'');oldest=model.get('oldest',0)
   if model.get('slow',False):
    model['inflight']=model.get('inflight',0)+1;model['max_inflight']=max(model.get('max_inflight',0),model['inflight']);time.sleep(.8);model['inflight']-=1
   begin=max(cursor,oldest) if boot=='1' else oldest;chunk=data[begin-oldest:begin-oldest+model.get('chunk',256)]
   return self.respond(200,{'boot_id':'1','execution_id':str(model.get('out_run',1)),'cursor':str(begin),'next_cursor':str(begin+len(chunk)),'lost':str(max(0,oldest-cursor) if boot=='1' else 0),'changed':boot=='1' and run!=str(model.get('out_run',1)),'reset':boot not in ('0','1'),'exhausted':False,'encoding':'base64','data':base64.b64encode(chunk).decode()})
  if path=='/api/program/status':
   if model['network_down']:
    self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
   if model['status_mode']=='malformed':return self.respond(200,b'{')
   if model['status_mode']=='unavailable':return self.respond(503,{'error':'unavailable'})
   if not model['available']:return self.respond(404,{'error':'missing'})
   return self.respond(200,{'state':model['state'],'last_result':model['result']})
  if path=='/api/health':return self.respond(200,{'status':'ok','http':'running'})
  if path in ('/api/system','/api/memory','/api/wifi/status'):return self.respond(200,{})
  if path=='/api/files':return self.respond(200,{'entries':[{'name':x[1:],'type':'file','size':len(y)} for x,y in model['files'].items()]})
  if path=='/api/files/content':
   name=parse_qs(urlparse(self.path).query)['path'][0]
   if name not in model['files']:return self.respond(404,{})
   return self.respond(200,model['files'][name].encode(), 'application/octet-stream',{'ETag':model['etag'][name]})
  f=ROOT/'web'/('index.html' if path=='/' else path.lstrip('/'))
  if not f.is_file():return self.respond(404,{})
  kind={'.html':'text/html','.js':'application/javascript','.css':'text/css'}.get(f.suffix,'text/plain');return self.respond(200,asset_bytes(f),kind)
 def do_PUT(self):
  path=parse_qs(urlparse(self.path).query)['path'][0];data=self.rfile.read(int(self.headers['Content-Length'])).decode()
  if model['delay_save']:time.sleep(.7)
  if model['save_error']:return self.respond(500,{'error':'save failed'})
  if model['conflict'] or self.headers.get('If-Match')!=model['etag'][path]:return self.respond(412,{'error':'conflict'})
  model['files'][path]=data;model['etag'][path]='"v2"';return self.respond(200,{'ok':True},headers={'ETag':'"v2"'})
 def do_POST(self):
  path=urlparse(self.path).path
  if path=='/api/program/run':
   body=json.loads(self.rfile.read(int(self.headers['Content-Length'])));rid=body.get('request_id');assert isinstance(rid,str) and len(rid)==36
   model['received'].append((rid,body['path']));model['transport'].append({'method':self.command,'path':path,'peer':self.client_address,'body':body,'length':self.headers.get('Content-Length'),'content_type':self.headers.get('Content-Type')})
   if rid in model['ids']:
    assert model['ids'][rid]==body['path']
    if model['run_mode']=='disconnect':self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
    return self.respond(202,{'accepted':True,'duplicate':True})
   model['ids'][rid]=body['path'];model['runs'].append(body['path'])
   if model['run_mode']=='busy':return self.respond(409,{'error':{}})
   if model['run_mode']=='incomplete':return self.respond(202,b'{')
   if model['run_mode']=='disconnect':self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
   if model['run_mode']=='timeout':model['state']='running';time.sleep(7);return self.respond(202,{'accepted':True,'state':'running'})
   model['state']='idle' if model['run_mode']=='short' else 'running';model['result']='ok' if model['run_mode']=='short' else model['result'];return self.respond(202,{'accepted':True,'state':model['state']})
  if path=='/api/program/stop':
   model['stops']+=1;model['state']='idle';model['result']='stopped'
   if model['stop_disconnect']:self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
   return self.respond(202,{'accepted':True,'state':'idle'})
  return self.respond(404,{})
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with socket.socket() as tmp:tmp.bind(('127.0.0.1',0));driver_port=tmp.getsockname()[1]
proc=subprocess.Popen([os.environ.get('GECKODRIVER','geckodriver'),'--port',str(driver_port)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);session=None

def call(method,path,data=None):
 req=urllib.request.Request(f'http://127.0.0.1:{driver_port}'+path,data=None if data is None else json.dumps(data).encode(),method=method,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=20) as r:return json.load(r)['value']
def js(code,*args):return call('POST',session+'/execute/sync',{'script':code,'args':args})
def wait(code,timeout=12):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if js(code):return
  time.sleep(.1)
 raise AssertionError('timeout: '+code)
def refresh_status():js('document.dispatchEvent(new Event("visibilitychange"))')
try:
 for _ in range(100):
  try:call('GET','/status');break
  except Exception:time.sleep(.1)
 sid=call('POST','/session',{'capabilities':{'alwaysMatch':{'browserName':'firefox','moz:firefoxOptions':{'args':['-headless']}}}})['sessionId'];session='/session/'+sid
 call('POST',session+'/window/rect',{'width':1366,'height':900});call('POST',session+'/url',{'url':f'http://127.0.0.1:{server.server_port}/'})
 wait('return !!document.querySelector("#split-explorer").getAttribute("aria-valuenow")')
 def elem(selector):return call('POST',session+'/element',{'using':'css selector','value':selector})['element-6066-11e4-a52e-4f735466cecf']
 def drag(selector,x,y):
  el=elem(selector);origin={'element-6066-11e4-a52e-4f735466cecf':el}
  call('POST',session+'/actions',{'actions':[{'type':'pointer','id':'mouse','parameters':{'pointerType':'mouse'},'actions':[{'type':'pointerMove','duration':0,'origin':origin,'x':0,'y':0},{'type':'pointerDown','button':0},{'type':'pointerMove','duration':250,'origin':'pointer','x':x,'y':y},{'type':'pointerUp','button':0}]}]});call('DELETE',session+'/actions')
 def value(selector):return int(js('return document.querySelector(arguments[0]).getAttribute("aria-valuenow")',selector))
 def key(selector,k):js('document.querySelector(arguments[0]).dispatchEvent(new KeyboardEvent("keydown",{key:arguments[1],bubbles:true,cancelable:true}))',selector,k)
 x=value('#split-explorer');y=value('#split-terminal');drag('#split-explorer',100,0);drag('#split-terminal',0,-70);assert value('#split-explorer')>x+70 and value('#split-terminal')>y+45
 el=elem('#split-explorer');origin={'element-6066-11e4-a52e-4f735466cecf':el};js('document.querySelector("#split-explorer").addEventListener("pointerdown",e=>window.__splitPointer=e.pointerId,{once:true})')
 call('POST',session+'/actions',{'actions':[{'type':'pointer','id':'cancelmouse','parameters':{'pointerType':'mouse'},'actions':[{'type':'pointerMove','duration':0,'origin':origin,'x':0,'y':0},{'type':'pointerDown','button':0}]}]});assert js('return document.body.classList.contains("resizing-panels")')
 js('document.querySelector("#split-explorer").dispatchEvent(new PointerEvent("pointercancel",{pointerId:window.__splitPointer,bubbles:true}))');assert not js('return document.body.classList.contains("resizing-panels")');call('DELETE',session+'/actions')
 js('document.querySelector("#split-explorer").dispatchEvent(new MouseEvent("dblclick",{bubbles:true}))');assert abs(value('#split-explorer')-x)<3
 drag('#split-explorer',100,0)
 saved=(value('#split-explorer'),value('#split-terminal'));call('POST',session+'/refresh',{});wait('return !!document.querySelector("#split-explorer").getAttribute("aria-valuenow")');assert abs(value('#split-explorer')-saved[0])<3 and abs(value('#split-terminal')-saved[1])<3
 for sel in ('#split-explorer','#split-terminal'):
  key(sel,'Home');assert value(sel)==int(js('return document.querySelector(arguments[0]).getAttribute("aria-valuemin")',sel))
  key(sel,'End');assert value(sel)==int(js('return document.querySelector(arguments[0]).getAttribute("aria-valuemax")',sel))
  key(sel,'Enter')
 call('POST',session+'/window/rect',{'width':1100,'height':800});assert js('return document.documentElement.scrollWidth<=document.documentElement.clientWidth')
 for theme in ('auto','light','dark'):
  assert js('let e=document.querySelector("#theme");e.value=arguments[0];e.dispatchEvent(new Event("change"));let s=getComputedStyle(document.querySelector("#terminal-body"));return s.backgroundColor==="rgb(0, 0, 0)" && s.color==="rgb(57, 255, 101)"',theme)
 model['available']=True;refresh_status();js('document.querySelector(\'[aria-label="Open hello.lua"]\').click()');wait('return !document.querySelector("#editor-text").hidden')
 js('let e=document.querySelector("#editor-text");e.focus();e.setSelectionRange(2,5)');before=js('let e=document.querySelector("#editor-text");return [e.value,e.selectionStart,e.selectionEnd]');drag('#split-explorer',-50,0);assert js('let e=document.querySelector("#editor-text");return [e.value,e.selectionStart,e.selectionEnd]')==before
 model.update(out_available=True,output=b'ongoing-output',chunk=2);refresh_status();wait('return document.querySelector("#terminal-body").textContent.includes("ongoing")',20);drag('#split-terminal',0,-35);wait('return document.querySelector("#terminal-body").textContent.includes("ongoing-output")',20)
 js('let r=document.createRange(),b=document.querySelector("#terminal-body");b.setAttribute("tabindex","-1");b.focus();r.selectNodeContents(b);let s=getSelection();s.removeAllRanges();s.addRange(r)')
 call('POST',session+'/actions',{'actions':[{'type':'key','id':'clipboard','actions':[{'type':'keyDown','value':'\ue009'},{'type':'keyDown','value':'c'},{'type':'keyUp','value':'c'},{'type':'keyUp','value':'\ue009'}]}]});call('DELETE',session+'/actions')
 js('let e=document.querySelector("#editor-text");e.focus();e.setSelectionRange(e.value.length,e.value.length)');eid=elem('#editor-text');call('POST',session+'/element/'+eid+'/value',{'text':'\ue009v\ue000'});assert js('return document.querySelector("#editor-text").value.includes("ongoing-output")')
 js('let e=document.querySelector("#editor-text");e.value=arguments[0];e.dispatchEvent(new Event("input",{bubbles:true}))',before[0])
 js('window.onbeforeunload=null;localStorage.setItem("zam-explorer-width","NaN");localStorage.setItem("zam-terminal-height","5")');call('POST',session+'/refresh',{});wait('return !!document.querySelector("#split-explorer").getAttribute("aria-valuenow")');assert .20<float(js('return document.querySelector("#split-explorer").getAttribute("aria-valuenow")'))/float(js('return document.querySelector(".workspace").getBoundingClientRect().width'))<.30
 js("document.querySelector('[data-view=system]').click()");assert js('return !document.querySelector("#system").hidden && document.querySelector("#lab").hidden')
 js("document.querySelector('[data-view=lab]').click()");wait('return !document.querySelector("#lab").hidden && !!document.querySelector("#split-explorer").getAttribute("aria-valuenow")')
 js('Object.defineProperty(window,"localStorage",{configurable:true,get(){throw new Error("blocked")}});window.ZamLayout()')
 call('POST',session+'/window/rect',{'width':390,'height':844});assert js('return getComputedStyle(document.querySelector("#split-explorer")).display==="none" && document.documentElement.scrollWidth<=document.documentElement.clientWidth')
 print('Firefox layout: drag, bounds, keyboard, reload, invalid preferences, resize, themes, selection and mobile PASS')
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
