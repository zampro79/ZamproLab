'use strict';
(() => {
  const $ = s => document.querySelector(s);
  const values = {};
  let busy = false, timer = null, lastSuccess = null;
  const set = (key, value) => document.querySelectorAll(`[data-value="${key}"]`).forEach(el => { el.textContent = value; });
  const text = v => typeof v === 'string' && v.trim() ? v : '—';
  const number = v => typeof v === 'number' && Number.isFinite(v) && v >= 0 ? v : null;
  const bytes = v => number(v) === null ? '—' : `${(v / 1024).toFixed(1)} KiB`;
  const uptime = v => { const n=number(v); if(n===null)return '—'; const s=Math.floor(n/1000); return `${Math.floor(s/3600)}h ${Math.floor(s/60)%60}m ${s%60}s`; };
  const state = (id) => { const el=$('#connection'); el.textContent=({connecting:'Connecting',online:'Online',offline:'Offline',error:'Error'})[id]; el.className=`connection ${id}`; };
  function render() {
    const s=values.system || {}, m=values.memory || {}, w=values.wifi || {}, h=values.health || {};
    ['name','version','target','idf_version'].forEach(k=>set(k,text(s[k])));
    set('cores', number(s.chip?.cores) === null ? '—' : String(s.chip.cores));
    set('revision', number(s.chip?.revision) === null ? '—' : `${Math.floor(s.chip.revision / 100)}.${s.chip.revision % 100}`);
    set('flash_size',bytes(s.flash_size)); set('uptime',uptime(s.uptime_ms));
    ['heap_free','heap_internal_free','heap_internal_largest_block','heap_minimum_free','heap_external_free'].forEach(k=>set(k,bytes(m[k])));
    ['configured_mode','effective_mode','sta_ssid','sta_ip','ap_ssid','ap_ip'].forEach(k=>set(k,text(w[k])));
    set('sta_state', typeof w.sta_connected === 'boolean' ? (w.sta_connected ? 'Connected' : 'Not connected') : '—');
    set('fallback',typeof w.fallback === 'boolean' ? (w.fallback ? 'Active' : 'No') : '—');
    set('ap_clients',number(w.ap_clients) === null ? '—' : String(w.ap_clients));
    set('http',h.status === 'ok' && h.http === 'running' ? 'Operational' : '—');
    $('#target').textContent=text(s.target);
  }
  async function fetchAPI(path,options={}) {
    const {timeout=15000,...requestOptions}=options;const controller=new AbortController();const timeoutId=setTimeout(()=>controller.abort(),timeout);
    try { const r=await fetch(path,{...requestOptions,signal:controller.signal,cache:'no-store'}); if(!r.ok) { const e=new Error('HTTP');e.http=true;e.status=r.status;throw e; } return r; } finally { clearTimeout(timeoutId); }
  }
  async function request(path) { const r=await fetchAPI(path); return r.json(); }
  async function refresh() {
    if(busy || document.hidden)return; busy=true; clearTimeout(timer);
    try {
      if(!lastSuccess) state('connecting');
      let health;
      try { health=await request('/api/health'); if(health.status!=='ok'||health.http!=='running') throw Object.assign(new Error('health'),{http:true}); values.health=health; state('online'); }
      catch(e) { state(e.http || e instanceof SyntaxError ? 'error' : 'offline'); $('#freshness').textContent=lastSuccess ? 'Data outdated' : 'Data unavailable'; render(); return; }
      const results=await Promise.allSettled([request('/api/system'),request('/api/memory'),request('/api/wifi/status')]);
      ['system','memory','wifi'].forEach((k,i)=>{if(results[i].status==='fulfilled') values[k]=results[i].value;});
      if(results.some(r=>r.status==='rejected')) { state('error'); $('#freshness').textContent='Some data outdated'; }
      else { lastSuccess=Date.now(); $('#freshness').textContent='Data updated'; }
      render();
    } finally { busy=false; if(!document.hidden) timer=setTimeout(refresh,5000); }
  }
  const theme=$('#theme'); theme.value=document.documentElement.dataset.theme || 'auto';
  theme.addEventListener('change',()=>{ try { localStorage.setItem('zam-theme',theme.value); document.documentElement.dataset.theme=theme.value; } catch(e) { theme.value='auto';document.documentElement.dataset.theme='auto'; } });
  let activeView='lab', onViewChange=()=>{};
  function view(name){ if(!['lab','system','settings'].includes(name))name='lab'; activeView=name; $('#lab').hidden=name!=='lab';$('#system').hidden=name!=='system';$('#settings').hidden=name!=='settings';document.querySelectorAll('[data-view]').forEach(b=>{const active=b.dataset.view===name||(b.dataset.view==='system'&&name==='settings');b.classList.toggle('active',active);if(active)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current');}); onViewChange(); }
  document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>{if(b.dataset.view!==activeView && !canLeave())return;location.hash=b.dataset.view;view(b.dataset.view);}));
  window.addEventListener('hashchange',()=>{const next=location.hash.slice(1);if(next!==activeView && !canLeave()){history.replaceState(null,'','#'+activeView);return;}view(next);}); view(location.hash.slice(1));
  document.querySelectorAll('[data-panel]').forEach(b=>b.addEventListener('click',()=>{document.querySelectorAll('[data-panel]').forEach(x=>x.classList.toggle('selected',x===b));document.querySelectorAll('[data-pane]').forEach(x=>x.classList.toggle('mobile-active',x.dataset.pane===b.dataset.panel));}));
  $('[data-pane="editor"]').classList.add('mobile-active');
  $('#terminal-toggle').addEventListener('click',e=>{const body=$('#terminal-body');body.hidden=!body.hidden;e.currentTarget.setAttribute('aria-expanded',String(!body.hidden));e.currentTarget.textContent=body.hidden?'Expand':'Collapse';});
  // LAB: all names and contents enter the DOM as text, never as HTML.
  const lab={dir:'/',library:false,seq:0,entries:[],file:null,etag:null,original:'',dirty:false,busy:false,sample:null};
  const editor=$('#editor-text'), status=$('#file-status');
  let execution={render(){},start(){},viewChanged(){},visibilityChanged(){}};
  const highlight=window.ZamHighlight?window.ZamHighlight(editor,lab):{render(){}};
  const terminal=window.ZamTerminal({isActive:()=>!document.hidden && activeView==='lab'});
  const layout=window.ZamLayout?window.ZamLayout():{refresh(){}};
  function canLeave(){if(lab.busy){message('Saving in progress.');return false;}return !lab.dirty || confirm('There are unsaved changes. Continue without saving?');}
  function message(t){status.textContent=t;}
  function reason(e){return ({400:'Invalid name or request',404:'File not found',409:'Operation conflict',403:'Operation not permitted',412:'File changed: reopen before saving',413:'File too large',500:'Device error',507:'Not enough space'})[e.status] || (e.http?'HTTP '+e.status:e.name==='AbortError'?'Timed out':'Connection unavailable');}
  function url(p){return '/api/files/content?path='+encodeURIComponent(p);}
  async function moduleIsText(path){const b=await (await fetchAPI(url(path))).arrayBuffer();return b.byteLength<=32768&&new Uint8Array(b)[0]!==27;}
  function join(dir,name){return (dir==='/'?'':dir)+'/'+name;}
  function validName(name){return /^[A-Za-z0-9._-]{1,63}$/.test(name) && name!=='.' && !name.startsWith('..') && !name.startsWith('.zam');}
  function validPath(path){return path.length<=192 && !/^\/(web|user)(\/|$)/.test(path);}
  function dirtyUI(){lab.dirty=!!lab.file && !editor.hidden && editor.value!==lab.original;$('#save').disabled=!lab.dirty||lab.busy;$('#file-title').textContent=lab.file?' / '+(lab.dirty?'● ':'')+lab.file:' / No file open';execution.render();highlight.render();}
  function clearEditor(){lab.sample=null;editor.readOnly=false;lab.file=null;lab.etag=null;lab.original='';lab.dirty=false;editor.hidden=true;$('#editor-notice').hidden=true;$('#editor-empty').hidden=false;$('#undo').disabled=true;$('#redo').disabled=true;dirtyUI();}
  function button(label,title,action){const b=document.createElement('button');b.textContent=label;b.title=title;b.setAttribute('aria-label',title);b.addEventListener('click',action);return b;}
  const list=window.ZamFileList({lab,fetchAPI,message,reason,join,button,enter,openFile,download,renameItem,removeItem:(...args)=>removeItem(...args),validPath,isText:moduleIsText});
  async function enter(path){if(!canLeave())return;clearEditor();lab.dir=path;await list();}
  async function openFile(path){if(!canLeave())return;lab.sample=null;editor.readOnly=false;lab.busy=true;editor.disabled=true;dirtyUI();message('Opening…');try{const r=await fetchAPI(url(path));const tag=r.headers.get('ETag');if(!tag)throw new Error('ETag missing');const bytes=new Uint8Array(await r.arrayBuffer());let text;try{text=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(bytes);if(/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(text))throw new Error('binary');}catch(e){lab.file=path;lab.etag=tag;lab.original='';lab.dirty=false;editor.hidden=true;$('#editor-empty').hidden=true;$('#editor-notice').hidden=false;$('#editor-notice').textContent='Binary or non-UTF-8 file: download only.';dirtyUI();await list();return;}lab.file=path;lab.etag=tag;editor.value=text;lab.original=editor.value;editor.hidden=false;$('#editor-empty').hidden=true;$('#editor-notice').hidden=true;$('#undo').disabled=false;$('#redo').disabled=false;dirtyUI();await list();}catch(e){message('Open failed: '+reason(e));}finally{lab.busy=false;editor.disabled=false;dirtyUI();}}
  async function save(){if(!lab.file||!lab.dirty||lab.busy)return false;const path=lab.file,content=editor.value,tag=lab.etag;lab.busy=true;editor.disabled=true;dirtyUI();message('Saving…');try{const r=await fetchAPI(url(path),{method:'PUT',headers:{'If-Match':tag,'Content-Type':'application/octet-stream'},body:new TextEncoder().encode(content)});const next=r.headers.get('ETag');if(!next)throw new Error('ETag missing');if(lab.file!==path)throw new Error('file changed');lab.etag=next;lab.original=content;message('Saved.');return true;}catch(e){message('Save failed: '+reason(e));return false;}finally{editor.disabled=false;lab.busy=false;dirtyUI();}}
  async function create(folder){if(lab.busy){message('Operation in progress.');return;}const name=prompt(folder?'Folder name':'File name');if(name===null)return;const path=join(lab.dir,name);if(!validName(name)||!validPath(path)){message('Invalid name.');return;}try{if(folder)await fetchAPI('/api/files/mkdir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path})});else await fetchAPI(url(path),{method:'PUT',headers:{'If-None-Match':'*'},body:new Uint8Array()});await list();message('Created: '+name);}catch(e){message('Create failed: '+reason(e));}}
  async function download(path,name){if(lab.busy){message('Operation in progress.');return;}try{const r=await fetchAPI(url(path));const blob=await r.blob();const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);message('Download ready: '+name);}catch(e){message('Download failed: '+reason(e));}}
  async function renameItem(path,name){if(lab.busy){message('Operation in progress.');return;}const n=prompt('New name',name);if(n===null||n===name)return;const target=join(lab.dir,n);if(!validName(n)||!validPath(target)){message('Invalid name.');return;}if(lab.file && (lab.file===path||lab.file.startsWith(path+'/')) && lab.dirty && !confirm('Open file has unsaved changes. Rename and keep it open?'))return;try{await fetchAPI('/api/files/rename',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({from:path,to:target})});if(lab.file&&(lab.file===path||lab.file.startsWith(path+'/')))lab.file=target+lab.file.slice(path.length);dirtyUI();await list();message('Renamed: '+n);}catch(e){message('Rename failed: '+reason(e));}}
  const {removeItem,upload}=window.ZamFileActions({lab,editor,dirtyUI,fetchAPI,url,list:()=>list(),clearEditor,message,reason,join,validName,validPath});
  execution=window.ZamExecution({lab,editor,save,fetchAPI,validName,isActive:()=>!document.hidden && activeView==='lab'});
  onViewChange=()=>{execution.viewChanged();terminal.changed();layout.refresh();};
  const samplesUI=window.ZamSamplesUI({lab,editor,highlight,renderExecution:()=>execution.render(),canLeave,clearEditor,button,message,join,validName,validPath,fetchAPI,url,list,openFile,dirtyUI,reason});
  const importLibrary=window.ZamLibraryImport({lab,editor,fetchAPI,url,validName,validPath,message,list,clearEditor,dirtyUI,reason});
  $('#import-library').addEventListener('click',()=>$('#import-library-input').click());$('#import-library-input').addEventListener('change',e=>{const f=e.target.files[0];e.target.value='';importLibrary(f);});
  $('#import-library-input').addEventListener('cancel',()=>message('No file selected.'));
  $('#show-samples').addEventListener('click',()=>{lab.library=false;lab.seq++;samplesUI.showSamples();});$('#show-user-files').addEventListener('click',()=>{lab.library=false;lab.dir='/';list();});$('#show-libraries').addEventListener('click',()=>{if(!canLeave())return;clearEditor();lab.library=true;lab.dir='/lib';list();});
  // The backend reserves /autorun mutations atomically; polling is UI feedback only.
  let autorunEpoch=0,autorunPending=false;
  const autorunCopy=$('#autorun-copy'),autorunRemove=$('#autorun-remove');
  function autorunDestination(path){if(!path||!path.endsWith('.lua'))return null;const name=path.slice(path.lastIndexOf('/')+1);return validName(name)?'/autorun/'+name:null;}
  async function autorunRefresh(){
    const epoch=++autorunEpoch,path=lab.file,dest=autorunDestination(path);autorunCopy.disabled=true;autorunRemove.disabled=true;
    if(!dest||lab.busy||autorunPending||editor.hidden||lab.dirty)return;
    try{const st=await (await fetchAPI('/api/program/status')).json();if(epoch!==autorunEpoch)return;
      if(!st.autorun||['waiting','scanning','running'].includes(st.autorun.state))return;
      let exists=false;try{const d=await (await fetchAPI('/api/files?path=%2Fautorun')).json();if(!Array.isArray(d.entries))throw new Error('invalid directory');exists=d.entries.some(x=>x.type==='file'&&x.name===dest.slice(9));}catch(e){if(e.status!==404)throw e;}
      if(epoch!==autorunEpoch||lab.file!==path||lab.busy||lab.dirty)return;
      autorunCopy.disabled=path.startsWith('/autorun/');autorunRemove.disabled=!exists;
    }catch(_){} }
  async function mutateAutorun(remove){
    if(autorunPending||lab.busy||lab.dirty)return;const path=lab.file,dest=autorunDestination(path);if(!dest||(!remove&&path.startsWith('/autorun/')))return;
    autorunPending=true;autorunCopy.disabled=true;autorunRemove.disabled=true;let submitted=false;
    try{const st=await (await fetchAPI('/api/program/status')).json();if(!st.autorun||['waiting','scanning','running'].includes(st.autorun.state))throw Object.assign(new Error('busy'),{status:409,http:true});
      let tag=null;try{tag=(await fetchAPI(url(dest))).headers.get('ETag');if(!tag)throw new Error('ETag missing');}catch(e){if(e.status!==404)throw e;}
      if(remove){if(!tag){message('Autorun copy not found.');return;}if(!confirm('Remove '+dest+' from Autorun?'))return;}
      else if(tag&&!confirm('Replace '+dest+' in Autorun?'))return;
      if(lab.file!==path||lab.dirty)throw new Error('Selection changed');submitted=true;
      if(remove){await fetchAPI('/api/files?path='+encodeURIComponent(dest),{method:'DELETE',headers:{'If-Match':tag}});try{await fetchAPI(url(dest));throw new Error('Removal not confirmed');}catch(e){if(e.status!==404)throw e;}if(lab.file===dest)clearEditor();message('Removed from Autorun: '+dest);}
      else{const headers={'Content-Type':'application/json'};headers[tag?'If-Match':'If-None-Match']=tag||'*';const r=await fetchAPI('/api/autorun/copy',{method:'POST',headers,body:JSON.stringify({source:path})});const expected=r.headers.get('ETag');const check=await fetchAPI(url(dest));if(!expected||check.headers.get('ETag')!==expected)throw new Error('Copy verification failed');message('Copied to Autorun: '+dest+'. Runs on next boot.');}
      if(lab.dir==='/autorun')await list();
    }catch(e){message(e.status===409?'Autorun is active, storage is busy, or the directory limit was reached.':(submitted&&!e.http?'Autorun outcome unconfirmed: ':'Autorun operation failed: ')+reason(e));}
    finally{autorunPending=false;autorunRefresh();}
  }
  autorunCopy.addEventListener('click',()=>mutateAutorun(false));autorunRemove.addEventListener('click',()=>mutateAutorun(true));
  editor.addEventListener('input',()=>autorunRefresh());setInterval(autorunRefresh,2000);autorunRefresh();
  $('#new-file').addEventListener('click',()=>create(false));$('#new-folder').addEventListener('click',()=>create(true));$('#refresh-files').addEventListener('click',list);$('#parent-dir').addEventListener('click',()=>enter(lab.dir.slice(0,lab.dir.lastIndexOf('/'))||'/'));$('#upload').addEventListener('click',()=>$('#upload-input').click());$('#upload-input').addEventListener('change',e=>{const files=Array.from(e.target.files);e.target.value='';upload(files);});$('#save').addEventListener('click',save);editor.addEventListener('input',dirtyUI);$('#undo').addEventListener('click',()=>{editor.focus();document.execCommand('undo');dirtyUI();});$('#redo').addEventListener('click',()=>{editor.focus();document.execCommand('redo');dirtyUI();});window.addEventListener('beforeunload',e=>{if(lab.dirty){e.preventDefault();e.returnValue='';}});list();
  document.addEventListener('visibilitychange',()=>{clearTimeout(timer);execution.visibilityChanged();terminal.changed();if(!document.hidden)refresh();});
  highlight.render();execution.start();terminal.start();
  refresh();
})();
