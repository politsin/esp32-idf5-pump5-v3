function $(id){return document.getElementById(id);}

function escHtml(s){return s.replace(/[&<>]/g,(c)=>c==='&'?'&amp;':(c==='<'?'&lt;':'&gt;'));}

function ansiToHtml(s){
  let out=''; let i=0; let open=false; let cur=null;
  const cmap={30:'#000',31:'#d9534f',32:'#19c37d',33:'#f0ad4e',34:'#0275d8',35:'#8e44ad',36:'#5bc0de',37:'#ddd',90:'#666',91:'#ff6b6b',92:'#5af78e',93:'#f4f99d',94:'#57c7ff',95:'#ff6ac1',96:'#9aedfe',97:'#fff'};
  function setColor(code){
    const col=cmap[code]||null;
    if(cur===col) return;
    if(open){ out+='</span>'; open=false; }
    cur=col;
    if(col){ out+='<span style="color:'+col+'">'; open=true; }
  }
  while(i<s.length){
    const esc=s.indexOf('\x1b[',i);
    if(esc===-1){ out+=escHtml(s.slice(i)); break; }
    out+=escHtml(s.slice(i,esc));
    const m=/^\x1b\[([0-9;]*)m/.exec(s.slice(esc));
    if(m){
      let codes=m[1]?m[1].split(';').filter(Boolean).map(Number):[0];
      if(codes.length===0) codes=[0];
      let fg=null; let reset=false;
      for(const c of codes){
        if(c===0){ reset=true; fg=null; }
        else if(c===39){ fg=null; }
        else if((c>=30&&c<=37)||(c>=90&&c<=97)){ fg=c; }
      }
      if(reset) setColor(null);
      if(fg!==null) setColor(fg);
      i=esc+m[0].length; continue;
    }
    i=esc+2;
  }
  if(open) out+='</span>';
  return out;
}

async function refresh(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    if(!r.ok) return;
    const s=await r.json();
    for(const k of ['pump','p1','p2','p3','p4']){
      const el=$('lamp-'+k);
      if(!el) continue;
      el.classList.remove('on','off');
      el.classList.add(s[k]?'on':'off');
    }
  }catch(e){}
}

async function refreshInfo(){
  try{
    const r=await fetch('/info.json',{cache:'no-store'});
    if(!r.ok) return;
    const j=await r.json();
    if($('ip')) $('ip').textContent=j.ip||'—';
    if($('mac')) $('mac').textContent=j.mac||'—';
    if($('rssi')) $('rssi').textContent=(j.rssi_dbm!=null)?(j.rssi_dbm+' dBm'):'—';
    if($('heap')) $('heap').textContent=(j.free_heap!=null)?(j.free_heap+' bytes'):'—';
    if($('uptime')) $('uptime').textContent=(j.uptime_s!=null)?(j.uptime_s+' s'):'—';
    if($('app')) $('app').textContent=j.app||'—';
    if($('ver')) $('ver').textContent=j.version||'—';
    if($('idf')) $('idf').textContent=j.idf||'—';
  }catch(e){}
}

async function toggle(id){
  try{ await fetch('/api/toggle?id='+encodeURIComponent(id),{method:'POST'}); }catch(e){}
  await refresh();
}

async function uploadFw(){
  const el=$('fw');
  const msg=$('ota-msg');
  if(!el||!el.files||!el.files[0]){ if(msg) msg.textContent='Выберите .bin файл'; return; }
  const f=el.files[0];
  if(msg) msg.textContent='Загрузка...';
  try{
    const ab=await f.arrayBuffer();
    const r=await fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:ab});
    if(!r.ok){ const t=await r.text(); if(msg) msg.textContent='Ошибка: '+t; return; }
    if(msg) msg.textContent='ОК, перезагрузка...';
  }catch(e){
    if(msg) msg.textContent='Ошибка загрузки';
  }
}

let logSince=0;
let logInFlight=false;
let logEnabled=false;
let logTimer=null;
let logAbort=null;

function setLogUi(){
  const btn=$('btn-log-toggle');
  const wrap=$('logwrap');
  const hint=$('log-hint');
  if(btn) btn.textContent = logEnabled ? 'Выключить' : 'Включить';
  if(wrap) wrap.style.display = logEnabled ? 'block' : 'none';
  if(hint) hint.style.display = logEnabled ? 'none' : 'block';
}

function stopLog(){
  logEnabled=false;
  if(logTimer){ clearTimeout(logTimer); logTimer=null; }
  if(logAbort){ try{ logAbort.abort(); }catch(e){} logAbort=null; }
  setLogUi();
}

function startLog(){
  if(logEnabled) return;
  logEnabled=true;
  setLogUi();
  if($('logbox')) $('logbox').textContent = '';
  logSince=0;
  pollLog();
}

async function pollLog(){
  if(!logEnabled) return;
  if(logInFlight) return;
  logInFlight=true;
  logAbort = new AbortController();
  try{
    const r=await fetch('/api/log?since='+logSince,{cache:'no-store', signal: logAbort.signal});
    if(!r.ok) return;
    const t=await r.text();
    const lines=t.split(/\r?\n/);
    if(lines.length===0) return;
    const m=/^next=(\d+)(?:\s+lost=1)?$/.exec((lines[0]||'').trim());
    if(!m) return;
    const next=parseInt(m[1]||'0',10)||0;
    if(next>logSince) logSince=next;
    const box=$('logbox'); if(!box) return;
    const data=lines.slice(1).filter(x=>x&&x.length>0).reverse();
    let html='';
    for(const l of data){ html += ansiToHtml(l) + '\n'; }
    if(html){ box.insertAdjacentHTML('afterbegin', html); box.scrollTop=0; }
  }catch(e){
    // AbortError — нормально при выключении журнала/смене вкладки
  }finally{
    logInFlight=false;
    logAbort=null;
    if(logEnabled){
      logTimer = setTimeout(pollLog, 700);
    }
  }
}

document.addEventListener('click',(e)=>{
  const t=e.target;
  if(t && t.dataset && t.dataset.toggle){ toggle(t.dataset.toggle); }
});
if($('btn-ota')) $('btn-ota').addEventListener('click', uploadFw);
if($('btn-log-toggle')) $('btn-log-toggle').addEventListener('click', ()=>{ logEnabled ? stopLog() : startLog(); });
setLogUi();

setInterval(refresh, 600); refresh();
setInterval(refreshInfo, 1500); refreshInfo();


