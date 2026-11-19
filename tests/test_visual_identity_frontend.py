#!/usr/bin/env python3
"""Check packaged identity in Firefox at desktop, tablet and mobile sizes."""
import base64,json,os,socket,subprocess,threading,time,urllib.request
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from frontend_asset_fixture import asset_bytes
ROOT=Path(__file__).resolve().parent.parent
OUT=Path(os.environ.get('VISUAL_SCREENSHOTS','/tmp/zam-visual-12x'));OUT.mkdir(parents=True,exist_ok=True)
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  path=self.path.split('?')[0]
  if path.startswith('/api/'):
   data=b'{}';kind='application/json'
  else:
   f=ROOT/'web'/('index.html' if path=='/' else path.lstrip('/'))
   if not f.is_file():self.send_error(404);return
   data=asset_bytes(f);kind={'.html':'text/html','.css':'text/css','.js':'text/javascript'}.get(f.suffix,'text/plain')
  self.send_response(200);self.send_header('Content-Type',kind);self.end_headers();self.wfile.write(data)
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
p=subprocess.Popen(['geckodriver','--port',str(port)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);session=None
def call(method,path,data=None):
 req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=None if data is None else json.dumps(data).encode(),method=method,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=30) as response:return json.load(response)['value']
def js(code):return call('POST',session+'/execute/sync',{'script':code,'args':[]})
try:
 for _ in range(100):
  try:call('GET','/status');break
  except Exception:time.sleep(.1)
 sid=call('POST','/session',{'capabilities':{'alwaysMatch':{'browserName':'firefox','moz:firefoxOptions':{'args':['-headless']}}}})['sessionId'];session='/session/'+sid
 for name,width,height in [('desktop',1366,900),('tablet',820,1000),('mobile',390,844)]:
  call('POST',session+'/window/rect',{'width':width,'height':height});call('POST',session+'/url',{'url':f'http://127.0.0.1:{server.server_port}/'})
  time.sleep(1)
  if name=='mobile':
   js("document.body.innerHTML='<iframe id=\"mobile-frame\" src=\"/\" style=\"width:390px;height:740px;border:0\"></iframe>';document.body.style.margin='0'")
   frame=call('POST',session+'/element',{'using':'css selector','value':'#mobile-frame'})
   call('POST',session+'/frame',{'id':frame});time.sleep(1)
  original_background=None
  for theme in ('dark','light','auto'):
   js(f'document.documentElement.dataset.theme="{theme}"')
   result=js('''let l=document.querySelector('.logo'),b=document.querySelector('.zamprolab-banner'),a=l.getBoundingClientRect(),r=b.getBoundingClientRect(),e=b.parentElement.getBoundingClientRect(); return {loaded:l.complete&&l.naturalWidth>0,logo:[a.width,a.height],banner:[r.width,r.height],parent:[e.width,e.left,e.right],rect:[r.left,r.right],background:getComputedStyle(b).backgroundImage,contain:getComputedStyle(b).backgroundSize,body:document.documentElement.scrollWidth,viewport:innerWidth};''')
   assert result['loaded'] and result['logo']==[40,40],result
   assert result['banner'][0]>0 and abs(result['banner'][0]/result['banner'][1]-3)<.02,result
   if name in ('desktop','tablet'):assert abs(result['banner'][0] - {'desktop':400,'tablet':350}[name]) < 2, result
   assert result['rect'][0]>=result['parent'][1] and result['rect'][1]<=result['parent'][2] and result['contain']=='contain',result
   assert result['background'].startswith('url("data:image/png;base64,'),result
   if original_background is None:original_background=result['background']
   else:assert result['background']==original_background, result
   raw=result['background'].split('base64,',1)[1].split('")',1)[0];assert base64.b64decode(raw).startswith(b'\x89PNG\r\n\x1a\n')
   shot=call('GET',session+'/screenshot');(OUT/f'{name}-{theme}.png').write_bytes(base64.b64decode(shot))
   print(name,theme,json.dumps({k:v for k,v in result.items() if k!='background'}),flush=True)
  if name=='mobile':call('POST',session+'/frame',{'id':None})
finally:
 if session:
  try:call('DELETE',session)
  except Exception:pass
 try:p.terminate();p.wait(timeout=5)
 except Exception:pass
 server.shutdown()
