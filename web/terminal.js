'use strict';
window.ZamTerminal=({isActive})=>{
  const body=document.querySelector('#terminal-body'),state=document.querySelector('#terminal-state'),clear=document.querySelector('#terminal-clear');
  let boot='0',run='0',cursor='0',decoder=new TextDecoder('utf-8'),timer=null,busy=false,epoch=0,absent=false;
  const limit=65536;
  function append(text){if(!text)return;const follow=body.scrollHeight-body.scrollTop-body.clientHeight<24;body.textContent=(body.textContent+text).slice(-limit);if(follow)body.scrollTop=body.scrollHeight;}
  function schedule(ms){clearTimeout(timer);if(isActive()&&!absent)timer=setTimeout(poll,ms);}
  async function poll(){
    clearTimeout(timer);if(!isActive()||absent||busy)return;busy=true;const generation=epoch;
    let delay=500;
    const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),4000);
    try{
      const url='/api/program/output?boot_id='+boot+'&execution_id='+run+'&cursor='+cursor+'&limit=256';
      const response=await fetch(url,{signal:controller.signal,cache:'no-store'});
      if(response.status===404){if(generation===epoch){absent=true;state.textContent='Lua runtime unavailable';}return;}
      if(response.status===409){if(generation===epoch){boot='0';run='0';cursor='0';decoder=new TextDecoder('utf-8');append('\n[Output state reset]\n');}return;}
      if(!response.ok)throw new Error('HTTP');
      const result=await response.json();
      if(generation!==epoch)return;
      const number=x=>typeof x==='string' && /^(0|[1-9][0-9]{0,19})$/.test(x);
      if(!result || !number(result.boot_id)||!number(result.execution_id)||!number(result.cursor)||!number(result.next_cursor)||!number(result.lost)||result.encoding!=='base64'||typeof result.data!=='string'||typeof result.changed!=='boolean'||typeof result.reset!=='boolean')throw new Error('invalid response');
      const bytes=Uint8Array.from(atob(result.data),c=>c.charCodeAt(0));
      if(bytes.length>256 || BigInt(result.next_cursor)-BigInt(result.cursor)!==BigInt(bytes.length))throw new Error('invalid cursor');
      if(boot!=='0' && !result.reset && BigInt(result.cursor)!==BigInt(cursor)+BigInt(result.lost))throw new Error('invalid sequence');
      if(result.reset || result.changed){decoder=new TextDecoder('utf-8');append('\n['+(result.reset?'Output service restarted':'New execution')+']\n');}
      if(result.lost!=='0'){decoder=new TextDecoder('utf-8');append('\n[Output lost: '+result.lost+' byte]\n');}
      append(decoder.decode(bytes,{stream:true}));boot=result.boot_id;run=result.execution_id;cursor=result.next_cursor;state.textContent='Output updated';
      if(result.exhausted)state.textContent='Output cursor exhausted; restart required';
    }catch(e){if(generation===epoch){state.textContent='Output connection unavailable';delay=5000;}}
    finally{clearTimeout(timeout);busy=false;if(generation===epoch)schedule(delay);else if(isActive()&&!absent)schedule(0);}
  }
  clear.addEventListener('click',()=>{body.textContent='';});
  return {start(){state.textContent='Connecting to output…';schedule(0);},changed(){epoch++;clearTimeout(timer);if(isActive()){absent=false;schedule(0);}}};
};

window.ZamSamplesUI=({lab,editor,highlight,renderExecution,canLeave,clearEditor,button,message,join,validName,validPath,fetchAPI,url,list,openFile,dirtyUI,reason})=>{
  const $=s=>document.querySelector(s);
  function showSamples(){
    const samples=window.ZamSamples || {}; const list=$('#file-list');list.replaceChildren();$('#current-dir').textContent='Samples (read-only)';$('#parent-dir').disabled=true;
    for(const name of Object.keys(samples).sort()){
      const row=document.createElement('div');row.className='file-row';
      const open=button(name,'Open sample '+name,()=>openSample(name));open.className='file-name';
      const copy=button('Copy','Copy sample '+name+' in UserFS',()=>copySample(name));row.append(open,copy);list.append(row);
    }
    if(!Object.keys(samples).length)message('Samples unavailable in WebFS.');else message('Read-only originals: copy a sample to the current UserFS folder.');
  }
  function openSample(name){
    if(!canLeave())return;const source=(window.ZamSamples || {})[name];if(typeof source!=='string')return;
    clearEditor();lab.sample=name;editor.value=source;editor.hidden=false;editor.readOnly=true;$('#editor-empty').hidden=true;$('#file-title').textContent=' / Samples / '+name+' (read-only)';
    $('#editor-notice').hidden=false;$('#editor-notice').textContent='Read-only original. Copy to UserFS to edit and run.';highlight.render();renderExecution();
  }
  async function copySample(name){
    if(lab.busy || !canLeave())return;const source=(window.ZamSamples || {})[name];if(typeof source!=='string')return;
    const target=join(lab.dir,name);if(!validName(name)||!validPath(target)){message('Invalid path.');return;}
    lab.busy=true;try{
      let headers={'If-None-Match':'*','Content-Type':'application/octet-stream'};
      try{const existing=await fetchAPI(url(target));const tag=existing.headers.get('ETag');if(!tag)throw new Error('ETag missing');if(!confirm('File '+target+' exists. Replace it?'))return;headers={'If-Match':tag,'Content-Type':'application/octet-stream'};}
      catch(e){if(e.status!==404)throw e;}
      await fetchAPI(url(target),{method:'PUT',headers,body:new TextEncoder().encode(source)});
      lab.busy=false;await list();await openFile(target);message('Copied to UserFS: '+target);
    }catch(e){message('Copy failed: '+reason(e));}finally{lab.busy=false;dirtyUI();}
  }
  return {showSamples};
};

window.ZamImportBytes=async f=>{let r;try{r=await f.arrayBuffer();}catch(_){throw new Error('file read failed');}const b=new Uint8Array(r);if(b.length!==f.size)throw new Error('file read failed');return b;};
window.ZamImportDirectories=async(parts,fetchAPI)=>{let parent='';for(const name of parts.slice(0,-1)){const r=await fetchAPI('/api/files?path='+encodeURIComponent(parent||'/'));const d=await r.json();if(!d||!Array.isArray(d.entries))throw new Error('invalid directory response');const found=d.entries.find(e=>e.name===name);parent+='/'+name;if(found&&found.type!=='directory')throw Object.assign(new Error('path is not a directory'),{status:409});if(!found)await fetchAPI('/api/files/mkdir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:parent})});}};
