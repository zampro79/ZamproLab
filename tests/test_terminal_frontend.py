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
 call('POST',session+'/url',{'url':f'http://127.0.0.1:{server.server_port}/'})
 wait('return document.querySelector("#terminal-state").textContent.includes("unavailable")')
 n=model['requests'];time.sleep(1.5);assert model['requests']==n
 model.update(out_available=True,output=b'A\xe2\x82\xac\x00<svg onload=alert(1)>\xff',chunk=2,available=True)
 refresh_status();wait('return document.querySelector("#terminal-body").textContent.includes("<svg")',20)
 assert js('return document.querySelector("#terminal-body").textContent.includes("A€\u0000")')
 assert js('return !document.querySelector("#terminal-body svg")')
 wait('return document.querySelector("#terminal-body").textContent.includes("�")')
 js('document.querySelector("#terminal-clear").click()');assert js('return document.querySelector("#terminal-body").textContent')==''
 model['out_run']=2;model['output']+=b'Z';wait('return document.querySelector("#terminal-body").textContent.includes("New execution")',12)
 model['oldest']=len(model['output'])+8;model['output']=b'perdita';wait('return document.querySelector("#terminal-body").textContent.includes("Output lost")',12)
 model['fail_output']=True;wait('return document.querySelector("#terminal-state").textContent.includes("unavailable")',12)
 model['fail_output']=False;wait('return document.querySelector("#terminal-state").textContent==="Output updated"',12)
 model['slow']=True;time.sleep(3);assert model['max_inflight']==1;model['slow']=False
 model['chunk']=256;model['output']+=b'Q'*70000
 js('window.__originalTimer=window.setTimeout;window.setTimeout=(fn,ms,...args)=>window.__originalTimer(fn,ms===500?1:ms,...args)')
 wait('return document.querySelector("#terminal-body").textContent.length===65536',40)
 assert js('return document.querySelector("#terminal-body").textContent.length')<=65536
 js('let b=document.querySelector("#terminal-body");b.scrollTop=0');time.sleep(.5)
 assert js('return document.querySelector("#terminal-body").scrollTop')<24
 js('let b=document.querySelector("#terminal-body");b.scrollTop=b.scrollHeight');time.sleep(.5)
 assert js('let b=document.querySelector("#terminal-body");return b.scrollHeight-b.scrollTop-b.clientHeight<24')
 js('window.setTimeout=window.__originalTimer')
 js('document.querySelector(\'[aria-label="Open hello.lua"]\').click()');wait('return !document.querySelector("#editor-text").hidden')
 assert js('return document.querySelector(".editor-shell").classList.contains("highlight-active")')
 assert js('return document.querySelector(".editor-gutter").getAttribute("aria-hidden") === "true" && document.querySelector("#editor-text").wrap === "off"')
 assert js('return !!document.querySelector(".editor-cursor-position")')
 for text,offset,expected in [('',0,'Ln 1, Col 1'),('alpha\nbeta\n',0,'Ln 1, Col 1'),('alpha\nbeta\n',5,'Ln 1, Col 6'),('alpha\nbeta\n',6,'Ln 2, Col 1'),('alpha\nbeta\n',10,'Ln 2, Col 5'),('alpha\nbeta\n',11,'Ln 3, Col 1'),('a\r\nb\rc',4,'Ln 3, Col 1')]:
  js('let e=document.querySelector("#editor-text");e.value=arguments[0];e.setSelectionRange(arguments[1],arguments[1]);e.dispatchEvent(new Event("input",{bubbles:true}))',text,offset)
  assert js('return document.querySelector(".editor-cursor-position").textContent')==expected,(text,offset,js('return document.querySelector(".editor-cursor-position").textContent'))
 js('let e=document.querySelector("#editor-text");e.value="abc\\ndef";e.setSelectionRange(1,6,"backward");e.dispatchEvent(new Event("select"))')
 assert js('return document.querySelector(".editor-cursor-position").textContent')=='Ln 1, Col 2'
 js('let e=document.querySelector("#editor-text");e.setSelectionRange(1,6,"forward");e.dispatchEvent(new Event("select"))')
 assert js('return document.querySelector(".editor-cursor-position").textContent')=='Ln 2, Col 3'
 js('let e=document.querySelector("#editor-text");e.value="single line";e.dispatchEvent(new Event("input",{bubbles:true}))')
 assert js('return document.querySelector(".editor-line-numbers").textContent')=='1'
 widths=[]
 for count in (1,10,100,1000):
  value='\n'.join('line '+str(i) for i in range(count)) if count>1 else ''
  js('let e=document.querySelector("#editor-text");e.value=arguments[0];e.dispatchEvent(new Event("input",{bubbles:true}))',value)
  lines=js('return document.querySelector(".editor-line-numbers").textContent.split("\\n")')
  assert lines==[str(i) for i in range(1,count+1)]
  widths.append(js('return document.querySelector(".editor-gutter").getBoundingClientRect().width'))
 assert all(a<b for a,b in zip(widths,widths[1:])),widths
 js('let e=document.querySelector("#editor-text");e.value="x".repeat(4000)+"\\n"+"short\\n".repeat(200);e.dispatchEvent(new Event("input",{bubbles:true}));e.scrollTop=1000;e.scrollLeft=1000;e.dispatchEvent(new Event("scroll"))')
 metrics=js('let e=document.querySelector("#editor-text"),n=document.querySelector(".editor-line-numbers"),g=document.querySelector(".editor-gutter");return [e.scrollTop,e.scrollLeft,new DOMMatrixReadOnly(getComputedStyle(n).transform).m42,getComputedStyle(e).lineHeight,getComputedStyle(n).lineHeight,g.getBoundingClientRect().right,e.getBoundingClientRect().left]')
 assert metrics[0]>0 and metrics[1]>0 and abs(metrics[0]+metrics[2])<1 and metrics[3]==metrics[4] and abs(metrics[5]-metrics[6])<1,metrics
 js('let e=document.querySelector("#editor-text");e.value="one\\ntwo\\nthree";e.dispatchEvent(new Event("input",{bubbles:true}))')
 assert js('return document.querySelector(".editor-line-numbers").textContent')=='1\n2\n3'
 js('let e=document.querySelector("#editor-text");e.value="one\\nthree";e.dispatchEvent(new Event("input",{bubbles:true}))')
 assert js('return document.querySelector(".editor-line-numbers").textContent')=='1\n2'
 js('let e=document.querySelector("#editor-text");e.value=arguments[0];e.dispatchEvent(new Event("input",{bubbles:true}));e.scrollTop=0;e.scrollLeft=0',model['files']['/hello.lua'])
 element=call('POST',session+'/element',{'using':'css selector','value':'#editor-text'});eid=element['element-6066-11e4-a52e-4f735466cecf'];ep=session+'/element/'+eid
 call('POST',ep+'/click',{})
 wait('let e=document.querySelector("#editor-text"),a=e.value.slice(0,e.selectionEnd).split("\\n");return document.querySelector(".editor-cursor-position").textContent==="Ln "+a.length+", Col "+(a[a.length-1].length+1)')
 js('let e=document.querySelector("#editor-text");e.focus();e.value="abc";e.setSelectionRange(3,3);e.dispatchEvent(new Event("input",{bubbles:true}))')
 call('POST',ep+'/value',{'text':'\ue012'})
 wait('return document.querySelector(".editor-cursor-position").textContent==="Ln 1, Col 3"')
 call('POST',ep+'/clear',{});call('POST',ep+'/value',{'text':'print("Ω")\nX'});call('POST',ep+'/value',{'text':'\ue003'})
 assert js('return document.querySelector("#editor-text").value')=='print("Ω")\n'
 assert js('let e=document.querySelector("#editor-text");e.setSelectionRange(0,5);return e.selectionStart===0 && e.selectionEnd===5')
 js('document.querySelector("#undo").click()');assert js('return document.querySelector("#editor-text").value')!='print("Ω")\n'
 js('document.querySelector("#redo").click()');assert js('return document.querySelector("#editor-text").value')=='print("Ω")\n'
 assert js('let e=document.querySelector("#editor-text"),p=e.selectionDirection==="backward"?e.selectionStart:e.selectionEnd,a=e.value.slice(0,p).split("\\n");return document.querySelector(".editor-cursor-position").textContent==="Ln "+a.length+", Col "+(a[a.length-1].length+1)')
 call('POST',ep+'/value',{'text':'\ue009a\ue000'});call('POST',ep+'/value',{'text':'\ue009c\ue000'});call('POST',ep+'/value',{'text':'\ue009\ue010\ue000'});call('POST',ep+'/value',{'text':'\ue009v\ue000'})
 assert js('return document.querySelector("#editor-text").value')=='print("Ω")\nprint("Ω")\n'
 assert js('return document.querySelector(".editor-line-numbers").textContent')=='1\n2\n3'
 assert js('return document.querySelector(".editor-cursor-position").textContent')=='Ln 3, Col 1'
 call('POST',ep+'/value',{'text':'\ue004'});assert js('return document.activeElement.id!=="editor-text"')
 call('POST',session+'/window/rect',{'width':390,'height':844});assert js('return getComputedStyle(document.querySelector(".mobile-nav")).display!=="none"')
 assert js('return document.querySelector(".editor-line-numbers").textContent')=='1\n2\n3'
 mobile=js('let e=document.querySelector("#editor-text"),g=document.querySelector(".editor-gutter");return [getComputedStyle(e).lineHeight,getComputedStyle(g).lineHeight,g.getBoundingClientRect().right,e.getBoundingClientRect().left]')
 assert mobile[0]==mobile[1] and abs(mobile[2]-mobile[3])<1,mobile
 assert js('let e=document.querySelector("#editor-text"),p=document.querySelector(".editor-cursor-position");return e.getBoundingClientRect().bottom<=p.getBoundingClientRect().top+1 && p.getBoundingClientRect().width>0')
 call('POST',session+'/window/rect',{'width':1366,'height':768})
 for theme in ('light','dark','auto'):
  assert js('let e=document.querySelector("#theme");e.value=arguments[0];e.dispatchEvent(new Event("change",{bubbles:true}));return document.documentElement.dataset.theme===arguments[0]',theme)
 timings=[]
 for size in (1024,10240,50000,100000):
  timings.append(js('let e=document.querySelector("#editor-text");e.value="local a = 123 -- x\\n".repeat(Math.ceil(arguments[0]/19)).slice(0,arguments[0]);let t=performance.now();e.dispatchEvent(new Event("input",{bubbles:true}));return performance.now()-t;',size))
  assert js('return document.querySelector(".editor-shell").classList.contains("highlight-active")')==(size<=50000)
 pathological=js('let e=document.querySelector("#editor-text");e.value="--[[".repeat(12000);let t=performance.now();e.dispatchEvent(new Event("input",{bubbles:true}));return performance.now()-t;')
 assert pathological<1000, pathological
 print('Terminal Firefox: absence, no retry, incremental UTF-8/NUL, hostile HTML, clear, run change, loss, 503, reconnect, no overlap and DOM limit PASS; highlight timings ms:',timings,'unfinished long comment:',pathological)
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
