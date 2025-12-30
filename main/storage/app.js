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

async function refreshStats(){
  try{
    const r=await fetch('/api/stats',{cache:'no-store'});
    if(!r.ok) return;
    const j=await r.json();
    if(!j||!j.ok) return;
    // Блок "Тики"
    if($('ticks')) $('ticks').textContent = (j.ticks!=null)?String(j.ticks):'—';
    if($('target')) $('target').textContent = (j.water_target!=null)?String(j.water_target):'—';
    if($('st-today')) $('st-today').textContent = (j.banks_today!=null)?String(j.banks_today):'—';
    if($('st-total')) $('st-total').textContent = (j.banks_total!=null)?String(j.banks_total):'—';
    if($('st-in-today')) $('st-in-today').value = (j.banks_today!=null)?String(j.banks_today):'';
    if($('st-in-total')) $('st-in-total').value = (j.banks_total!=null)?String(j.banks_total):'';
    if($('st-msg')) $('st-msg').textContent = '—';
  }catch(e){}
}

async function refreshTickLevel(){
  const el=$('tick-level');
  if(!el) return;
  try{
    const r=await fetch('/api/ticks/level',{cache:'no-store'});
    if(!r.ok) return;
    const j=await r.json();
    if(!j||!j.ok) return;
    const lvl = (j.level!=null) ? (j.level ? 'HIGH(1)' : 'LOW(0)') : '—';
    el.textContent = lvl;
  }catch(e){}
}

async function saveStats(){
  const msg=$('st-msg');
  try{
    const today = parseInt(($('st-in-today')||{}).value||'0',10);
    const total = parseInt(($('st-in-total')||{}).value||'0',10);
    if(msg) msg.textContent='Сохранение...';
    const qs = `today=${encodeURIComponent(String(today))}&total=${encodeURIComponent(String(total))}`;
    const r=await fetch('/api/stats?'+qs,{method:'POST'});
    if(!r.ok){
      const t=await r.text();
      if(msg) msg.textContent='Ошибка: '+t;
      return;
    }
    if(msg) msg.textContent='OK';
    await refreshStats();
  }catch(e){
    if(msg) msg.textContent='Ошибка';
  }
}

async function refreshI2c(){
  try{
    const r=await fetch('/api/i2c',{cache:'no-store'});
    if(!r.ok) return;
    const j=await r.json();
    if(!j||!j.ok) return;
    if($('i2c-summary')) $('i2c-summary').textContent = j.summary || '—';
  }catch(e){}
}

async function refreshConfig(){
  try{
    const r=await fetch('/api/config',{cache:'no-store'});
    if(!r.ok) return;
    const j=await r.json();
    if(!j||!j.ok) return;
    if($('cfg-steps')) $('cfg-steps').value = (j.steps!=null)?String(j.steps):'';
    if($('cfg-enc')) $('cfg-enc').value = (j.encoder!=null)?String(j.encoder):'';
    if($('cfg-flush1')) $('cfg-flush1').value = (j.flush_valve_ms!=null)?String(j.flush_valve_ms):'';
    if($('cfg-flushall')) $('cfg-flushall').value = (j.flush_all_ms!=null)?String(j.flush_all_ms):'';
    if($('cfg-dryms')) $('cfg-dryms').value = (j.dry_run_timeout_ms!=null)?String(j.dry_run_timeout_ms):'';
    if($('cfg-drymin')) $('cfg-drymin').value = (j.dry_run_min_ticks!=null)?String(j.dry_run_min_ticks):'';
    if($('cfg-ticksrc')) $('cfg-ticksrc').value = (j.tick_source!=null)?String(j.tick_source):'1';
    if($('cfg-tickdeb')) $('cfg-tickdeb').value = (j.tick_min_interval_us!=null)?String(j.tick_min_interval_us):'';
    if($('cfg-tickgpio')) $('cfg-tickgpio').value = (j.tick_gpio!=null)?('GPIO'+String(j.tick_gpio)):'';
    if($('cfg-tickpull')) $('cfg-tickpull').value = (j.tick_pull!=null)?String(j.tick_pull):'0';
    if($('cfg-meta')) $('cfg-meta').textContent = (j.water_target!=null)?('water_target=' + j.water_target + ' ticks'):'—';
    if($('tick-meta')) $('tick-meta').textContent = (j.water_target!=null)?('water_target=' + j.water_target + ' ticks'):'—';
    if($('cfg-msg')) $('cfg-msg').textContent = '';
    if($('tick-msg')) $('tick-msg').textContent = '';
  }catch(e){}
}

async function saveConfig(){
  const msg=$('cfg-msg');
  try{
    const steps = parseInt(($('cfg-steps')||{}).value||'0',10)||0;
    const enc = parseInt(($('cfg-enc')||{}).value||'0',10)||0;
    const f1 = parseInt(($('cfg-flush1')||{}).value||'0',10)||0;
    const f2 = parseInt(($('cfg-flushall')||{}).value||'0',10)||0;
    const dryms = parseInt(($('cfg-dryms')||{}).value||'0',10)||0;
    const drymin = parseInt(($('cfg-drymin')||{}).value||'0',10)||0;
    const ticksrc = parseInt(($('cfg-ticksrc')||{}).value||'1',10);
    const tickdeb = parseInt(($('cfg-tickdeb')||{}).value||'0',10)||0;
    const tickpull = parseInt(($('cfg-tickpull')||{}).value||'0',10);
    if(msg) msg.textContent='Сохранение...';
    const qs = `steps=${encodeURIComponent(String(steps))}&encoder=${encodeURIComponent(String(enc))}&flush_valve_ms=${encodeURIComponent(String(f1))}&flush_all_ms=${encodeURIComponent(String(f2))}&dry_run_timeout_ms=${encodeURIComponent(String(dryms))}&dry_run_min_ticks=${encodeURIComponent(String(drymin))}&tick_source=${encodeURIComponent(String(ticksrc))}&tick_min_interval_us=${encodeURIComponent(String(tickdeb))}&tick_pull=${encodeURIComponent(String(tickpull))}`;
    const r=await fetch('/api/config?'+qs,{method:'POST'});
    if(!r.ok){
      const t=await r.text();
      if(msg) msg.textContent='Ошибка: '+t;
      return;
    }
    if(msg) msg.textContent='OK';
    await refreshConfig();
  }catch(e){
    if(msg) msg.textContent='Ошибка';
  }
}

async function resetTicks(){
  const msg=$('tick-msg') || $('cfg-msg');
  try{
    if(msg) msg.textContent='Сброс тиков...';
    const r=await fetch('/api/ticks/reset',{method:'POST'});
    if(!r.ok){
      const t=await r.text();
      if(msg) msg.textContent='Ошибка: '+t;
      return;
    }
    if(msg) msg.textContent='Тики сброшены';
    await refreshStats();
  }catch(e){
    if(msg) msg.textContent='Ошибка';
  }
}

async function saveTickConfig(){
  const msg=$('tick-msg');
  try{
    const ticksrc = parseInt(($('cfg-ticksrc')||{}).value||'1',10);
    const tickdeb = parseInt(($('cfg-tickdeb')||{}).value||'0',10)||0;
    const tickpull = parseInt(($('cfg-tickpull')||{}).value||'0',10);
    if(msg) msg.textContent='Сохранение...';
    const qs = `tick_source=${encodeURIComponent(String(ticksrc))}&tick_min_interval_us=${encodeURIComponent(String(tickdeb))}&tick_pull=${encodeURIComponent(String(tickpull))}`;
    const r=await fetch('/api/config?'+qs,{method:'POST'});
    if(!r.ok){
      const t=await r.text();
      if(msg) msg.textContent='Ошибка: '+t;
      return;
    }
    if(msg) msg.textContent='OK';
    await refreshConfig();
  }catch(e){
    if(msg) msg.textContent='Ошибка';
  }
}

function bit(v, b){ return ((v>>b)&1) ? 1 : 0; }

function buildIoexpGrid(){
  const root=$('ioexp-grid');
  if(!root) return;
  root.innerHTML='';

  // Визуальная раскладка по твоей схеме:
  // 00/17
  // 01/16
  // ...
  // 07/10
  const rows=[
    [0,15,'00','17'],
    [1,14,'01','16'],
    [2,13,'02','15'],
    [3,12,'03','14'],
    [4,11,'04','13'],
    [5,10,'05','12'],
    [6, 9,'06','11'],
    [7, 8,'07','10'],
  ];

  function mkCol(title, side){ // side 0 = left(bitLo), 1 = right(bitHi)
    const col=document.createElement('div');
    col.className='ioexp-col';
    const h=document.createElement('h3');
    h.textContent=title;
    col.appendChild(h);
    for(const r of rows){
      const b = side===0 ? r[0] : r[1];
      const pin = side===0 ? ('P'+r[2]) : ('P'+r[3]);

      const row=document.createElement('div');
      row.className='ioexp-row';

      const l=document.createElement('div');
      l.className='ioexp-pin';
      l.textContent=pin;

      const st=document.createElement('div');
      st.className='ioexp-state';
      st.id='ioexp-b'+b;
      st.dataset.ioexpBit=String(b);
      st.title='Клик: toggle (пишем 0/1 в shadow).';

      const left=document.createElement('div');
      left.className='ioexp-bit';
      left.textContent='bit '+b;

      const right=document.createElement('div');
      right.className='ioexp-val';
      right.textContent='—';

      st.appendChild(left);
      st.appendChild(right);

      const btn=document.createElement('button');
      btn.className='btn-ghost';
      btn.textContent='Toggle';
      btn.dataset.ioexpToggle=String(b);

      row.appendChild(l);
      row.appendChild(st);
      row.appendChild(btn);
      col.appendChild(row);
    }
    return col;
  }

  root.appendChild(mkCol('Левая сторона (00..07)',0));
  root.appendChild(mkCol('Правая сторона (17..10)',1));
}

async function ioexpFetch(){
  const r=await fetch('/api/ioexp',{cache:'no-store'});
  if(!r.ok) return null;
  return await r.json();
}

async function refreshIoexp(){
  try{
    const j=await ioexpFetch();
    if(!j||!j.ok) return;
    const meta=$('ioexp-meta');
    if(meta) meta.textContent = `port=${j.port} shadow=${j.shadow}`;

    // Используем реально прочитанный порт (j.port_u) для отображения
    const portU = (j.port_u>>>0) & 0xFFFF;
    for(let b=0;b<16;b++){
      const el=$('ioexp-b'+b);
      if(!el) continue;
      const v=bit(portU,b);
      el.classList.remove('high','low');
      el.classList.add(v? 'high':'low');
      const valEl=el.querySelector('.ioexp-val');
      if(valEl) valEl.textContent = v ? 'HIGH(1)' : 'LOW(0)';
    }
  }catch(e){}
}

async function ioexpToggleBit(b){
  try{ await fetch('/api/ioexp/set?bit='+encodeURIComponent(String(b))+'&toggle=1',{method:'POST'}); }catch(e){}
  await refreshIoexp();
  await refresh(); // обновим и старые лампочки (насос/клапаны)
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
    if($('build')) $('build').textContent=((j.build_date||'')+' '+(j.build_time||'')).trim()||'—';
    if($('sha')) $('sha').textContent=j.elf_sha8||'—';
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
  if(t && t.dataset && t.dataset.ioexpToggle){ ioexpToggleBit(t.dataset.ioexpToggle); }
});
if($('btn-ota')) $('btn-ota').addEventListener('click', uploadFw);
if($('btn-log-toggle')) $('btn-log-toggle').addEventListener('click', ()=>{ logEnabled ? stopLog() : startLog(); });
setLogUi();

buildIoexpGrid();
if($('btn-ioexp-refresh')) $('btn-ioexp-refresh').addEventListener('click', refreshIoexp);
if($('btn-i2c-refresh')) $('btn-i2c-refresh').addEventListener('click', refreshI2c);
if($('btn-cfg-reload')) $('btn-cfg-reload').addEventListener('click', refreshConfig);
if($('btn-cfg-save')) $('btn-cfg-save').addEventListener('click', saveConfig);
if($('btn-ticks-reset')) $('btn-ticks-reset').addEventListener('click', resetTicks);
if($('btn-tick-reload')) $('btn-tick-reload').addEventListener('click', refreshConfig);
if($('btn-tick-save')) $('btn-tick-save').addEventListener('click', saveTickConfig);
if($('btn-st-save')) $('btn-st-save').addEventListener('click', saveStats);

setInterval(refresh, 600); refresh();
setInterval(refreshInfo, 1500); refreshInfo();
setInterval(refreshIoexp, 350); refreshIoexp();
setInterval(refreshI2c, 1200); refreshI2c();
setInterval(refreshStats, 1500); refreshStats();
setInterval(refreshTickLevel, 3000); refreshTickLevel();
refreshConfig();


