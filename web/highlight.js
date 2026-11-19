'use strict';
window.ZamHighlight=(editor,lab)=>{
  const shell=document.createElement('div'),layer=document.createElement('pre'),gutter=document.createElement('div'),numbers=document.createElement('pre'),position=document.createElement('div');shell.className='editor-shell';layer.className='editor-highlight';gutter.className='editor-gutter';numbers.className='editor-line-numbers';position.className='editor-cursor-position';layer.setAttribute('aria-hidden','true');gutter.setAttribute('aria-hidden','true');gutter.append(numbers);editor.parentNode.insertBefore(shell,editor);shell.append(gutter,layer,editor,position);editor.wrap='off';
  const words=new Set('and break do else elseif end false for function goto if in local nil not or repeat return then true until while'.split(' '));
  const builtins=new Set('print pairs ipairs next type tostring tonumber error assert pcall xpcall select require collectgarbage zam sleep'.split(' '));
  const token=/--\[(=*)\[[\s\S]*?(?:\]\1\]|$)|--[^\n]*|\[(=*)\[[\s\S]*?(?:\]\2\]|$)|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'|\b(?:0x[\da-fA-F]+|\d+(?:\.\d+)?)\b|\b[A-Za-z_][A-Za-z_0-9]*\b/g;
  let previous=null,enabled=false,lineCount=0;
  function cursor(){
    const text=editor.value,end=editor.selectionDirection==='backward'?editor.selectionStart:editor.selectionEnd;
    let line=1,col=1;for(let i=0;i<end;i++){if(text[i]==='\r'){line++;col=1;if(i+1<end&&text[i+1]==='\n')i++;}else if(text[i]==='\n'){line++;col=1;}else col++;}
    position.textContent='Ln '+line+', Col '+col;
  }
  function render(){
    shell.hidden=editor.hidden;
    cursor();
    const text=editor.value;let count=1;for(let i=0;i<text.length;i++)if(text.charCodeAt(i)===10)count++;
    if(count!==lineCount){lineCount=count;shell.style.setProperty('--gutter-width','calc('+String(count).length+'ch + 20px)');numbers.textContent=Array.from({length:count},(_,i)=>i+1).join('\n');}
    numbers.style.transform='translateY('+(-editor.scrollTop)+'px)';
    const on=!editor.hidden && !!(lab.file || lab.sample) && (lab.file || lab.sample).endsWith('.lua') && text.length<=50000;
    shell.classList.toggle('highlight-active',on);
    if(!on){if(enabled)layer.replaceChildren();enabled=false;previous=null;return;}
    if(enabled && text===previous)return;enabled=true;previous=text;
    const fragment=document.createDocumentFragment();let last=0;
    for(const m of text.matchAll(token)){
      fragment.append(document.createTextNode(text.slice(last,m.index)));const x=m[0],kind=x.startsWith('--')?'comment':x[0]==='"'||x[0]==="'"||x[0]==='['?'str':/^\d/.test(x)?'num':words.has(x)?'kw':builtins.has(x)?'api':'';
      if(kind){const span=document.createElement('span');span.className=kind;span.textContent=x;fragment.append(span);}else fragment.append(document.createTextNode(x));last=m.index+x.length;
    }
    fragment.append(document.createTextNode(text.slice(last)+'\n'));layer.replaceChildren(fragment);layer.scrollTop=editor.scrollTop;layer.scrollLeft=editor.scrollLeft;
  }
  for(const event of ['input','click','keyup','select','focus'])editor.addEventListener(event,cursor);
  document.addEventListener('selectionchange',()=>{if(document.activeElement===editor)cursor();});
  editor.addEventListener('scroll',()=>{layer.scrollTop=editor.scrollTop;layer.scrollLeft=editor.scrollLeft;numbers.style.transform='translateY('+(-editor.scrollTop)+'px)';});return {render};
};

// Desktop splitter preferences are browser-local; panel content stays untouched.
window.ZamLayout=()=>{
  const grid=document.querySelector('.workspace'),vertical=document.querySelector('#split-explorer'),horizontal=document.querySelector('#split-terminal');
  const defaults=[.25,.30],keys=['zam-explorer-width','zam-terminal-height'];
  let ratios=keys.map((key,i)=>{try{const raw=localStorage.getItem(key);if(raw===null)return defaults[i];const n=Number(raw);return Number.isFinite(n)&&n>0&&n<1?n:defaults[i];}catch(e){return defaults[i];}});
  function bounds(i,r){return i===0?[160,Math.max(160,r.width-550)]:[140,Math.max(140,r.height-250)];}
  function refresh(){
    if(!matchMedia('(min-width: 1051px)').matches)return;
    const r=grid.getBoundingClientRect();if(!r.width||!r.height)return;
    [vertical,horizontal].forEach((bar,i)=>{const [min,max]=bounds(i,r),extent=i===0?r.width:r.height;const value=Math.max(min,Math.min(max,ratios[i]*extent));
      ratios[i]=value/extent;grid.style.setProperty(i===0?'--explorer-width':'--terminal-height',value+'px');
      bar.setAttribute('aria-valuemin',String(Math.round(min)));bar.setAttribute('aria-valuemax',String(Math.round(max)));bar.setAttribute('aria-valuenow',String(Math.round(value)));bar.setAttribute('aria-valuetext',Math.round(value)+' pixel');
    });
  }
  function store(i){try{localStorage.setItem(keys[i],String(ratios[i]));}catch(e){}}
  function change(i,value){const r=grid.getBoundingClientRect(),extent=i===0?r.width:r.height;if(!extent)return;ratios[i]=value/extent;refresh();}
  [vertical,horizontal].forEach((bar,i)=>{
    bar.addEventListener('pointerdown',e=>{if(e.button!==0)return;e.preventDefault();bar.setPointerCapture(e.pointerId);document.body.classList.add('resizing-panels');
      function move(ev){const r=grid.getBoundingClientRect();change(i,i===0?ev.clientX-r.left:r.bottom-ev.clientY);}
      function end(ev){if(ev.pointerId!==e.pointerId)return;bar.removeEventListener('pointermove',move);bar.removeEventListener('pointerup',end);bar.removeEventListener('pointercancel',end);bar.removeEventListener('lostpointercapture',end);document.body.classList.remove('resizing-panels');store(i);}
      bar.addEventListener('pointermove',move);bar.addEventListener('pointerup',end);bar.addEventListener('pointercancel',end);bar.addEventListener('lostpointercapture',end);
    });
    bar.addEventListener('dblclick',()=>{ratios[i]=defaults[i];refresh();store(i);});
    bar.addEventListener('keydown',e=>{const r=grid.getBoundingClientRect(),[min,max]=bounds(i,r),current=Number(bar.getAttribute('aria-valuenow'));let next;
      if(e.key==='Home')next=min;else if(e.key==='End')next=max;else if(e.key==='Enter'){ratios[i]=defaults[i];refresh();store(i);e.preventDefault();return;}
      else if(i===0&&e.key==='ArrowLeft'||i===1&&e.key==='ArrowDown')next=current-10;
      else if(i===0&&e.key==='ArrowRight'||i===1&&e.key==='ArrowUp')next=current+10;else return;
      e.preventDefault();change(i,next);store(i);
    });
  });
  window.addEventListener('resize',refresh);if(window.ResizeObserver)new ResizeObserver(refresh).observe(grid);refresh();return {refresh};
};

// Reuses the File Manager API and keeps source bytes as data.
window.ZamLibraryImport=({lab,editor,fetchAPI,url,validName,validPath,message,list,clearEditor,dirtyUI,reason})=>async file=>{
  if(lab.busy){message('Operation in progress.');return;}
  if(!file){message('No file selected.');return;}
  if(!file.name.endsWith('.lua')){message('Choose a .lua file.');return;}
  if(file.size>32768){message('Module source exceeds 32 KiB.');return;}
  if(!/^[A-Za-z_][A-Za-z0-9_]*\.lua$/.test(file.name)||!validName(file.name)){message('Invalid module file name.');return;}
  const path=prompt('Destination for '+file.name+':','/lib/'+file.name);if(path===null)return;
  const parts=path.startsWith('/')?path.slice(1).split('/'):[];
  if(parts.length<1||!validPath(path)||parts.some(p=>!validName(p))||!parts[parts.length-1].endsWith('.lua')){message('Invalid destination path.');return;}
  const base=parts[parts.length-1].slice(0,-4),names=[...parts.slice(0,-1),base];
  if(names.some(n=>!/^[A-Za-z_][A-Za-z0-9_]*$/.test(n))){message('Invalid module name in destination.');return;}
  const mod=names.join('.');
  if(lab.file===path&&lab.dirty){message('Open file has unsaved changes.');return;}
  if(!confirm('Import '+file.name+' to '+path+'?\nUse require("'+mod+'")'))return;
  lab.busy=true;editor.disabled=true;dirtyUI();let wrote=false,attempted=false;
  try{
    const bytes=await window.ZamImportBytes(file);
    await window.ZamImportDirectories(parts,fetchAPI);
    let headers={'If-None-Match':'*'};
    try{const r=await fetchAPI(url(path));const tag=r.headers.get('ETag');if(!tag)throw new Error('ETag missing');if(!confirm(path+' already exists. Replace it?')){message('Import cancelled.');return;}headers={'If-Match':tag};}
    catch(e){if(e.status!==404)throw e;}
    attempted=true;await fetchAPI(url(path),{method:'PUT',headers,body:bytes});wrote=true;
    const saved=new Uint8Array(await (await fetchAPI(url(path))).arrayBuffer());
    if(saved.length!==bytes.length||saved.some((v,i)=>v!==bytes[i]))throw new Error('verification mismatch');
    if(lab.file===path)clearEditor();await list(path);message('Imported '+path+'. Use require("'+mod+'").');
  }catch(e){const detail=e.http&&![400,404,409,412,413,500,507].includes(e.status)?'HTTP '+e.status:reason(e);message((wrote?'Upload accepted; verification failed: ':attempted&&!e.http?'Upload outcome unconfirmed: ':'Import failed: ')+(e.message==='verification mismatch'?'content differs':e.message==='file read failed'?'file read failed':detail));}
  finally{lab.busy=false;editor.disabled=false;dirtyUI();}
};
