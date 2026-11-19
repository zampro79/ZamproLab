'use strict';

window.ZamFileList=({lab,fetchAPI,message,reason,join,button,enter,openFile,download,renameItem,removeItem,validPath,isText})=>async imported=>{
const $=s=>document.querySelector(s), list=$('#file-list'),seq=++lab.seq;
if(typeof imported==='string' && lab.library){lab.dir=imported.slice(0,imported.lastIndexOf('/'))||'/';if(lab.dir!=='/lib'&&!lab.dir.startsWith('/lib/'))lab.library=false;}
const viewKey=(lab.library?'library:':'user:')+lab.dir;if(list.dataset.viewKey!==viewKey)list.replaceChildren();message('Loading…');try{
const requestedDir=lab.dir, requestedLibrary=lab.library;let data,missingLibrary=false;
try{data=await (await fetchAPI('/api/files?path='+encodeURIComponent(requestedDir))).json();}
catch(e){if(requestedLibrary&&requestedDir==='/lib'&&e.status===404){missingLibrary=true;data={entries:[]};}else throw e;}
if(seq!==lab.seq)return;
if(!data||!Array.isArray(data.entries))throw new Error('invalid JSON');lab.entries=data.entries;list.replaceChildren();list.dataset.viewKey=viewKey;
$('#current-dir').textContent=(lab.library?'Installed libraries: ':'')+lab.dir;
$('#parent-dir').disabled=lab.dir===(lab.library?'/lib':'/');
const ident=s=>/^[A-Za-z_][A-Za-z0-9_]*$/.test(s);
const entries=lab.entries.filter(item=>{
if(!item||typeof item.name!=='string'||!['file','directory'].includes(item.type))return false;
if(!lab.library)return true;
const path=join(lab.dir,item.name),part=item.type==='file'?item.name.slice(0,-4):item.name;
return validPath(path)&&ident(part)&&(item.type==='directory'||(item.name.endsWith('.lua')&&Number.isInteger(item.size)&&item.size<=32768));
});
for(let i=entries.length-1;lab.library&&i>=0;i--){if(seq!==lab.seq)return;if(entries[i].type==='file'&&!await isText(join(lab.dir,entries[i].name)))entries.splice(i,1);}if(seq!==lab.seq)return;
if(!entries.length){const p=document.createElement('p');p.textContent=lab.library?(missingLibrary?'Library directory not found. No installed libraries.':'No installed libraries.'):'No files. Create or upload one.';list.append(p);}
entries.slice().sort((a,b)=>(a.type==='directory'?0:1)-(b.type==='directory'?0:1)||a.name.localeCompare(b.name)).forEach(item=>{
const path=join(lab.dir,item.name),row=document.createElement('div'),actions=document.createElement('span');row.className='file-row';actions.className='file-actions';
const mod=lab.library&&item.type==='file'?path.slice(1,-4).replaceAll('/','.') : '';
const name=button((item.type==='directory'?'▸ ':'')+item.name+(mod?' · require("'+mod+'")':''),'Open '+item.name,()=>item.type==='directory'?enter(path):openFile(path));
name.className='file-name';name.classList.toggle('active',lab.file===path);row.append(name);
if(item.type==='file')actions.append(button('↓','Download '+item.name,()=>download(path,item.name)));
if(!lab.library)actions.append(button('✎','Rename '+item.name,()=>renameItem(path,item.name)));
if(!lab.library||item.type==='file')actions.append(button('×','Delete '+(mod||item.name),()=>removeItem(path,mod||item.name,item.type)));
row.append(actions);list.append(row);
});message('');
}catch(e){if(seq===lab.seq)message('List unavailable: '+reason(e));}
};

window.ZamFileActions=({lab,editor,dirtyUI,fetchAPI,url,list,clearEditor,message,reason,join,validName,validPath})=>{
  const removeItem=async(path,name,type)=>{
  if(lab.busy){message('Operation in progress.');return;}
  const library=lab.library;let tag,attempted=false,deleted=false;lab.busy=true;editor.disabled=true;dirtyUI();
  try{
    if(type==='file'){tag=(await fetchAPI(url(path))).headers.get('ETag');if(!tag)throw new Error('ETag missing');}
    if(!confirm('Delete '+(library?'library ':'')+name+'?'+(library?'':' Folders must be empty.')))return;
    const open=lab.file&&(lab.file===path||lab.file.startsWith(path+'/'));
    if(open&&lab.dirty&&!confirm('Open file has unsaved changes. Delete anyway?'))return;
    if(library){const now=(await fetchAPI(url(path))).headers.get('ETag');if(!now||now!==tag)throw Object.assign(new Error('changed'),{http:true,status:412});}
    attempted=true;await fetchAPI('/api/files?path='+encodeURIComponent(path),{method:'DELETE',headers:type==='file'?{'If-Match':tag}:{}});deleted=true;
    if(open)clearEditor();
    if(library){try{await fetchAPI(url(path));await list();message('Delete not confirmed: file still exists.');return;}catch(e){if(e.status!==404)throw e;}}
    await list();message('Deleted: '+name);
  }catch(e){message(e.status===412?'File changed: deletion cancelled.':(deleted?'Delete accepted; verification failed: ':attempted&&!e.http?'Delete outcome unconfirmed: ':'Delete failed: ')+reason(e));}finally{lab.busy=false;editor.disabled=false;dirtyUI();}
};
  async function upload(files){if(lab.busy){message('Operation in progress.');return;}let done=0;const results=[];const destination=lab.dir;lab.busy=true;try{for(const f of files){const path=join(destination,f.name);message('Upload '+f.name+' ('+done+'/'+files.length+')');if(!validName(f.name)||!validPath(path)||f.size>524288){results.push(f.name+': invalid name or over 512 KiB');continue;}if(lab.file===path && lab.dirty){results.push(f.name+': open file has changes');continue;}try{let headers={'If-None-Match':'*'};try{const r=await fetchAPI(url(path));const tag=r.headers.get('ETag');if(!tag)throw new Error('ETag missing');if(!confirm('File '+f.name+' exists. Replace it?'))continue;headers={'If-Match':tag};}catch(e){if(e.status!==404)throw e;}await fetchAPI(url(path),{method:'PUT',headers,body:f});done++;results.push(f.name+': complete');message('Upload complete: '+f.name+' ('+done+'/'+files.length+')');if(lab.file===path)clearEditor();}catch(e){results.push(f.name+': '+reason(e));}}}finally{lab.busy=false;}await list();message('Upload '+done+'/'+files.length+': '+results.join(' | '));}
  return {removeItem,upload};
};
