#!/usr/bin/gjs -m
import Gio from 'gi://Gio';
const assert=(v,m)=>{if(!v)throw Error(m)};
class Element {
  constructor(id=''){this.id=id;this.hidden=false;this.disabled=false;this.checked=false;this.value='';this.textContent='';this.children=[];this.events={};this.map={};}
  set innerHTML(s){for(const m of s.matchAll(/id="([^"]+)"/g))this.map['#'+m[1]]=new Element(m[1]);}
  addEventListener(n,f){(this.events[n]??=[]).push(f);}
  async fire(n,e={}){for(const f of this.events[n]||[])await f(e);}
  querySelector(s){assert(this.map[s],s);return this.map[s];}
  append(e){this.children.push(e);if(e.id==='settings')section=e;}
  replaceChildren(){this.children=[];}
}
let section=null;const main=new Element('main');const win=new Element('window');
globalThis.document={hidden:false,createElement:()=>new Element(),querySelector:s=>s==='main'?main:null,addEventListener:()=>{}};
globalThis.addEventListener=win.addEventListener.bind(win);globalThis.location={hash:''};globalThis.confirm=()=>true;globalThis.setTimeout=()=>1;globalThis.clearTimeout=()=>{};
globalThis.AbortController=class{constructor(){this.signal={};}abort(){}};
let calls=[],saved=null,mode='normal',acceptedObserved=false;
const wifi={configured_mode:'ap',effective_mode:'ap',sta_connected:false,sta_paused:false,sta_ssid:'',sta_ssid_valid_utf8:true,sta_ip:'',ap_ssid:'ZamproLab-test',ap_ip:'192.168.4.1'};
globalThis.fetch=async(path,opt={})=>{calls.push([path,opt.method||'GET']);let v={};
 if(path==='/api/wifi/status')v=wifi;
 else if(path==='/api/wifi/operation')v={boot_id:'boot',next_id:1,found:false};
 else if(path.startsWith('/api/wifi/operation?id='))v={boot_id:'boot',accepted:true,persistence:'saved',application:'applied',connection:'connected',error:0,disconnect_reason:0};
 else if(path==='/api/wifi/settings'){saved=JSON.parse(opt.body);if(mode==='settings403')return {ok:false,status:403,json:async()=>({error:'origin_forbidden'})};if(mode==='lost')throw Error('lost');v={accepted:true};}
 else if(path==='/api/wifi/scan'){if(mode==='scan403')return {ok:false,status:403,json:async()=>({error:'origin_forbidden'})};if(mode==='scan409')return {ok:false,status:409,json:async()=>({error:'wifi_busy_or_scanner_unavailable'})};if(mode==='scan400')return {ok:false,status:400,json:async()=>({error:'body_not_allowed'})};if(mode==='scan-network')throw Error('network');v={boot_id:'boot',scan_id:3};}
 else if(path==='/api/wifi/scan?id=3'){acceptedObserved=section.querySelector('#net-scan-message').textContent==='Scan request accepted.';v={boot_id:'boot',scan_id:3,pending:false,error:0,networks:[{ssid:'Cafe',selectable:true,hidden:false,rssi:-30,auth:3},{ssid:'[Non-UTF-8 SSID]',selectable:false,hidden:false,rssi:-60,auth:0},{ssid:'',selectable:false,hidden:true,rssi:-70,auth:3}]};}
 else throw Error('unexpected '+path);
 return {ok:true,json:async()=>v};};
const [ok,bytes]=Gio.File.new_for_path('web/settings.js').load_contents(null);assert(ok,'load');eval(new TextDecoder().decode(bytes));
assert(section&&section.hidden,'section exists and is initially hidden');section.hidden=false;location.hash='#settings';await win.fire('hashchange');await Promise.resolve();await Promise.resolve();
assert(!calls.some(c=>c[0]==='/api/wifi/scan'),'no automatic scan');
await section.querySelector('#net-scan').fire('click');const networks=section.querySelector('#net-networks').children;assert(networks.length===3,'network list');assert(acceptedObserved,'accepted feedback before result');assert(networks[1].disabled&&networks[2].disabled,'unsafe and hidden unselectable');await networks[0].fire('click');assert(section.querySelector('#net-ssid').value==='Cafe','selection');
section.querySelector('#net-password').value='secretpass';await section.querySelector('#net-form').fire('submit',{preventDefault(){}});assert(saved.ssid==='Cafe'&&saved.password==='secretpass'&&saved.password_action==='replace','settings submission');assert(section.querySelector('#net-password').value==='','password cleared');assert(section.querySelector('#net-operation').textContent.includes('connected'),'connection result');
for(const [test,expected] of [['scan403','origin security'],['scan409','Scanner busy'],['scan400','Invalid scan'],['scan-network','unconfirmed']]){mode=test;await section.querySelector('#net-scan').fire('click');assert(section.querySelector('#net-scan-message').textContent.includes(expected),'scan '+test);}
mode='settings403';section.querySelector('#net-ssid').value='Manual';section.querySelector('#net-action').value='keep';await section.querySelector('#net-form').fire('submit',{preventDefault(){}});assert(section.querySelector('#net-operation').textContent.includes('origin security'),'settings 403');
mode='lost';section.querySelector('#net-ssid').value='Manual';section.querySelector('#net-action').value='keep';const before=calls.filter(c=>c[0]==='/api/wifi/settings').length;await section.querySelector('#net-form').fire('submit',{preventDefault(){}});assert(saved.ssid==='Manual'&&saved.password_action==='keep'&&!('password' in saved),'manual and keep');assert(section.querySelector('#net-operation').textContent.includes('unconfirmed'),'uncertain outcome');assert(calls.filter(c=>c[0]==='/api/wifi/settings').length===before+1,'no automatic retry');
await section.querySelector('#settings-back').fire('click');assert(location.hash==='system','back navigation');
print('Settings frontend DOM mock PASS');
