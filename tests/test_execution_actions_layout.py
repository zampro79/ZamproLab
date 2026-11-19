#!/usr/bin/env python3
"""Exercise the existing LAB and module-import flow in headless Firefox."""
import hashlib, json, os, socket, subprocess, threading, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from frontend_asset_fixture import asset_bytes

ROOT=Path(__file__).resolve().parent.parent
model={'dirs':{'/'},'files':{},'puts':[],'runs':0,'fail':None,'corrupt':False,'delete_mode':None,'list_mode':None,'etag_change':False,'gets':0,'deletes':[],'delay_file':False,'requests':[],'delay_lib':False,'automatic':'completed','change_on_delete':False,'copy_calls':0}
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
  if path=='/api/program/status':return self.reply(200,{'state':'idle','last_result':None,'autorun':{'state':model['automatic']}})
  if path=='/api/program/output':return self.reply(404,{})
  if path=='/api/files':
   p=q.get('path',['/'])[0];model['requests'].append(p)
   if model['delay_lib'] and p=='/lib':time.sleep(1.5)
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
  if p=='/api/autorun/copy':
   model['copy_calls']+=1
   if model['automatic']=='running':return self.reply(409,{'error':'busy'})
   source=json.loads(data)['source'];dest='/autorun/'+source.rsplit('/',1)[1]
   if source not in model['files']:return self.reply(404,{})
   previous=model['files'].get(dest)
   if previous is not None and self.headers.get('If-Match')!='"'+hashlib.sha256(previous).hexdigest()+'"':return self.reply(412,{})
   if previous is None and self.headers.get('If-None-Match')!='*':return self.reply(412,{})
   model['dirs'].add('/autorun');model['files'][dest]=model['files'][source]
   return self.reply(200,{'ok':True},headers={'ETag':'"'+hashlib.sha256(model['files'][source]).hexdigest()+'"'})
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
  if model['change_on_delete'] and p.startswith('/autorun/'):model['files'][p]=b'changed concurrently';model['change_on_delete']=False
  if p.startswith('/autorun/') and self.headers.get('If-Match')!='"'+hashlib.sha256(model['files'][p]).hexdigest()+'"':return self.reply(412,{})
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
 wait('return !!document.querySelector("#autorun-remove")')
 for width in (1366,1100,800,500,390,320):
  call('POST',session+'/window/rect',{'width':max(width,500),'height':900})
  js('document.body.style.width=arguments[0]+"px";if(arguments[0]<=700)document.querySelector(`[data-panel="execution"]`).click()',width)
  for theme in ('light','dark'):
   js('document.documentElement.dataset.theme=arguments[0]',theme)
   result=js('''let p=document.querySelector('[data-pane="execution"] .panel-body'),a=document.querySelector('.execution .actions');let ids=['program-run','program-stop','autorun-copy','autorun-remove'];let rect=x=>{let r=x.getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom,width:r.width,height:r.height}};return {viewport:innerWidth,panel:rect(p),scroll:p.scrollWidth,client:p.clientWidth,actions:rect(a),buttons:ids.map(id=>({id,disabled:document.getElementById(id).disabled,...rect(document.getElementById(id))}))}''')
   b=result['buttons'];p=result['panel'];print('LAYOUT',width,theme,json.dumps(result),flush=True)
   assert result['scroll']<=result['client']+1
   assert all(x['width']>0 and x['height']>0 and x['left']>=p['left']-1 and x['right']<=p['right']+1 for x in b)
   assert abs(b[0]['top']-b[1]['top'])<2 and b[2]['top']>=b[0]['bottom']-1 and b[3]['top']>=b[2]['bottom']-1
   assert all(x['disabled'] for x in b)
 print('Execution panel Firefox: four buttons, three rows, overflow and disabled state PASS')
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:proc.terminate();proc.wait(timeout=5)
 except (OSError,subprocess.TimeoutExpired):pass
 server.shutdown();server.server_close()
