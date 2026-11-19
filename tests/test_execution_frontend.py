#!/usr/bin/env python3
"""Browser regression with local mocked HTTP; requires Firefox and geckodriver."""
import json
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
 call('POST',session+'/url',{'url':f'http://127.0.0.1:{server.server_port}/'})
 wait('return document.querySelector("#program-state").textContent.includes("unavailable")')
 assert js('return document.querySelector("#program-run").disabled && document.querySelector("#program-stop").disabled')
 model['available']=True;refresh_status();wait('return document.querySelector("#program-state").textContent==="Idle"')
 js('document.querySelector(\'[aria-label="Open hello.lua"]\').click()');wait('return !document.querySelector("#editor-text").hidden && !document.querySelector("#program-run").disabled')
 js('document.querySelector("#program-run").click();document.querySelector("#program-run").click()');wait('return document.querySelector("#program-state").textContent==="Running"');assert model['runs']==['/hello.lua']
 js('document.querySelector("#program-stop").click()');wait('return document.querySelector("#program-state").textContent==="Idle" && document.querySelector("#program-result").textContent==="Stopped"')
 for key,label in [('starting','Starting'),('stopping','Stopping'),('cleanup','Cleaning up')]:
  model['state']=key;refresh_status();wait('return document.querySelector("#program-state").textContent==='+json.dumps(label))
  assert js('return document.querySelector("#program-run").disabled')
  if key!='starting':assert js('return document.querySelector("#program-stop").disabled')
 model['state']='idle';model['run_mode']='short';refresh_status();wait('return document.querySelector("#program-state").textContent==="Idle"')
 js('document.querySelector("#program-run").click()');wait('return document.querySelector("#program-result").textContent==="Completed"');assert model['state']=='idle'
 model['run_mode']='busy';js('document.querySelector("#program-run").click()');wait('return document.querySelector("#program-message").textContent.includes("busy")')
 model['state']='idle';model['result']='syntax_error';refresh_status();wait('return document.querySelector("#program-result").textContent==="Syntax error"')
 model['run_mode']='running';before=len(model['runs'])
 js('window.confirm=()=>false;let e=document.querySelector("#editor-text");e.value="cancelled";e.dispatchEvent(new Event("input",{bubbles:true}));document.querySelector("#program-run").click()')
 wait('return document.querySelector("#program-message").textContent.includes("cancelled")');assert len(model['runs'])==before and js('return document.querySelector("#editor-text").value')=='cancelled'
 wait('return !document.querySelector("#program-run").disabled')
 model['save_error']=True
 js('window.confirm=()=>true;document.querySelector("#program-run").click()');wait('return document.querySelector("#file-status").textContent.includes("failed")');assert len(model['runs'])==before
 wait('return !document.querySelector("#program-run").disabled')
 model['save_error']=False;model['conflict']=True
 js('window.confirm=()=>true;let e=document.querySelector("#editor-text");e.value="unsaved";e.dispatchEvent(new Event("input",{bubbles:true}));document.querySelector("#program-run").click()')
 wait('return document.querySelector("#file-status").textContent.includes("changed")');assert len(model['runs'])==before and js('return document.querySelector("#editor-text").value')=='unsaved'
 wait('return !document.querySelector("#program-run").disabled')
 model['conflict']=False;model['delay_save']=True;js('document.querySelector("#program-run").click();document.querySelector(\'[aria-label="Open other.lua"]\').click()');wait('return document.querySelector("#program-state").textContent==="Running"');assert model['runs'][-1]=='/hello.lua' and model['files']['/hello.lua']=='unsaved'
 js('document.querySelector("#program-stop").click()');wait('return document.querySelector("#program-state").textContent==="Idle"')
 model['run_mode']='disconnect';before=len(model['runs']);received_before=len(model['received']);js('window.__fetchCalls=0;const savedFetch=window.fetch.bind(window);window.fetch=(...a)=>{if(String(a[0]).endsWith("/api/program/run"))window.__fetchCalls++;return savedFetch(...a)}');js('document.querySelector("#program-run").click()');wait('return document.querySelector("#program-message").textContent.includes("unconfirmed")');time.sleep(.8);assert len(model['runs'])==before+1,(before,len(model['runs']),model['runs']);assert js('return window.__fetchCalls')==1;assert len({x[0] for x in model['received'][received_before:]})==1;print('DISCONNECT fetch=1 HTTP=%d starts=1 details=%s'% (len(model['received'])-received_before,json.dumps(model['transport'][received_before:])))
 model['run_mode']='incomplete';wait('return !document.querySelector("#program-run").disabled');before=len(model['runs']);js('document.querySelector("#program-run").click()');wait('return document.querySelector("#program-message").textContent.includes("unconfirmed")');assert len(model['runs'])==before+1
 model['network_down']=True;refresh_status();wait('return document.querySelector("#program-state").textContent==="Status unavailable"');model['network_down']=False;refresh_status();wait('return document.querySelector("#program-state").textContent==="Idle"')
 model['status_mode']='malformed';refresh_status();wait('return document.querySelector("#program-message").textContent.includes("Invalid")')
 model['status_mode']='unavailable';refresh_status();wait('return document.querySelector("#program-state").textContent==="Status unavailable"')
 model['status_mode']='normal';refresh_status();wait('return document.querySelector("#program-state").textContent==="Idle"');assert js('return document.querySelector("#program-message").textContent')==''
 model['run_mode']='timeout';before=len(model['runs']);js('document.querySelector("#program-run").click()');wait('return document.querySelector("#program-message").textContent.includes("Timed out")',timeout=10);assert len(model['runs'])==before+1
 wait('return document.querySelector("#program-state").textContent==="Running"');model['stop_disconnect']=True;before=model['stops'];js('document.querySelector("#program-stop").click()');wait('return document.querySelector("#program-message").textContent.includes("unconfirmed")');time.sleep(.5);assert model['stops']>=before+1
 print('Frontend Firefox: availability, states, errors, short run, STOP, duplicate, ETag, save race, timeout and reconnect PASS')
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
