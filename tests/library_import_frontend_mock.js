'use strict';
const assert=(v,m)=>{if(!v)throw new Error(m||'assertion failed');};
assert.deepStrictEqual=(a,b)=>assert(JSON.stringify(a)===JSON.stringify(b),'values differ');
const ctx=window;
function scenario(options={}){
 const dirs=new Set(['/']),files=new Map(),calls=[],messages=[],answers=[...(options.confirms||[true])];
 if(options.existing){dirs.add('/lib');files.set('/lib/utils.lua',new Uint8Array([1,2,3]));}
 const lab={busy:false,file:null,dirty:false},editor={disabled:false};
 const err=status=>Object.assign(new Error('HTTP'),{status,http:true});
 const response=(data,tag)=>({json:async()=>data,headers:{get:k=>k==='ETag'?tag:null},arrayBuffer:async()=>Uint8Array.from(data).buffer});
 async function fetchAPI(url,opt={}){
  calls.push({url,method:opt.method||'GET',headers:opt.headers||{}});
  if(options.network)throw new Error('network');
  if(url.startsWith('/api/files?path=')){const p=decodeURIComponent(url.split('=')[1]);if(!dirs.has(p))throw err(404);return response({entries:[...dirs].filter(x=>x!=='/'&&x.slice(0,x.lastIndexOf('/'))===(p==='/'?'':p)).map(x=>({name:x.split('/').pop(),type:'directory'})).concat([...files.keys()].filter(x=>x.slice(0,x.lastIndexOf('/'))===(p==='/'?'':p)).map(x=>({name:x.split('/').pop(),type:'file'})))});}
  if(url==='/api/files/mkdir'){const p=JSON.parse(opt.body).path;dirs.add(p);return response({ok:true});}
  const p=decodeURIComponent(url.split('=')[1]);
  if(opt.method==='PUT'){
   if(options.putStatus)throw err(options.putStatus);
   if(files.has(p)&&opt.headers['If-None-Match'])throw err(412);
   if(files.has(p)&&opt.headers['If-Match']!=='tag')throw err(412);
   files.set(p,Uint8Array.from(opt.body));return response({ok:true});
  }
  if(!files.has(p))throw err(404);
  let data=files.get(p);if(options.corrupt&&calls.some(x=>x.method==='PUT'))data=new Uint8Array([99]);
  return response(data,'tag');
 }
 ctx.prompt=()=>options.path===undefined?'/lib/utils.lua':options.path;
 ctx.confirm=()=>answers.shift()??false;
 const run=ctx.window.ZamLibraryImport({lab,editor,fetchAPI,url:p=>'/api/files/content?path='+encodeURIComponent(p),validName:n=>/^[A-Za-z0-9._-]{1,63}$/.test(n)&&n!=='.'&&!n.startsWith('..')&&!n.startsWith('.zam'),validPath:p=>p.length<=192&&!/^\/(web|user)(\/|$)/.test(p),message:m=>messages.push(m),list:async()=>{},clearEditor:()=>{},dirtyUI:()=>{},reason:e=>({412:'File changed',507:'Not enough space',500:'Device error'})[e.status]||'Connection unavailable'});
 return {run,dirs,files,calls,messages,lab,editor};
}
const file=(name='utils.lua',bytes=[10,20,30],size=bytes.length)=>({name,size,arrayBuffer:async()=>Uint8Array.from(bytes).buffer});
(async()=>{
 let s=scenario();await s.run(file());assert(s.dirs.has('/lib'));assert.deepStrictEqual([...s.files.get('/lib/utils.lua')],[10,20,30]);assert(s.messages.at(-1).includes('require("lib.utils")'));assert(s.calls.some(c=>c.method==='PUT'&&c.headers['If-None-Match']==='*'));assert(!s.calls.some(c=>c.url.includes('/api/program/')));
 s=scenario();s.lab.file='/lib/utils.lua';s.lab.dirty=true;await s.run(file());assert(s.messages.at(-1).includes('unsaved changes'));assert(!s.calls.length);
 s=scenario();let release;const pending=new Promise(r=>{release=r;});const running=s.run({name:'utils.lua',size:1,arrayBuffer:()=>pending});await Promise.resolve();assert(s.editor.disabled);release(Uint8Array.from([42]).buffer);await running;assert(!s.editor.disabled);
 for(const [f,piece] of [[file('utils.txt'),'Choose a .lua'],[file('bad-name.lua'),'Invalid module'],[file('utils.lua',[],32769),'32 KiB'],[null,'No file']]){s=scenario();await s.run(f);assert(s.messages.at(-1).includes(piece));assert(!s.calls.length);}
 for(const path of ['/lib/../x.lua','/user/x.lua','relative.lua','/lib/bad-name.lua']){s=scenario({path});await s.run(file());assert(/Invalid (destination|module)/.test(s.messages.at(-1)));assert(!s.calls.length);}
 s=scenario({existing:true,confirms:[true,false]});await s.run(file());assert.deepStrictEqual([...s.files.get('/lib/utils.lua')],[1,2,3]);assert(!s.calls.some(c=>c.method==='PUT'));
 s=scenario({existing:true,confirms:[true,true]});await s.run(file());assert.deepStrictEqual([...s.files.get('/lib/utils.lua')],[10,20,30]);assert(s.calls.some(c=>c.headers['If-Match']==='tag'));
 for(const [option,part] of [[{putStatus:507},'Not enough space'],[{putStatus:500},'Device error'],[{putStatus:403},'HTTP 403'],[{putStatus:412},'File changed'],[{network:true},'Connection unavailable'],[{corrupt:true},'verification failed']]){s=scenario(option);await s.run(file());assert(s.messages.at(-1).includes(part));assert(!s.lab.busy);}
 s=scenario();await s.run({name:'utils.lua',size:1,arrayBuffer:async()=>{throw Error('read');}});assert(s.messages.at(-1).includes('file read failed'));
 window.__importTest='PASS';
})().catch(e=>{window.__importTest='FAIL: '+e.stack;});
