#pragma once
// Gemeinsames JavaScript beider Weboberflächen:
//  - req()/post(): Anfrage mit Timeout und automatischen Wiederholungen.
//    Aktionen tragen eine Request-ID (rid), die bei Wiederholungen gleich
//    bleibt – der Pico führt die Aktion deshalb nie doppelt aus.
//  - busy()/show(): Ladeanzeige und Meldungen.
//  - setLink(): Anzeige "Verbindung Browser ↔ Pico".

constexpr const char* WEB_SCRIPT_COMMON = R"JS(
const $=id=>document.getElementById(id);
const newRid=()=>Date.now().toString(36)+'-'+Math.random().toString(36).slice(2,8);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let linkFails=0;
function setLink(ok){
  linkFails=ok?0:linkFails+1;
  const el=$('link'); if(!el) return;
  const good=linkFails<2;
  el.textContent=good?'verbunden':'keine Verbindung – versuche erneut …';
  el.className='pill '+(good?'ok':'err');
}
async function req(url,opt={},tries=3,timeout=8000){
  let last;
  for(let i=0;i<tries;i++){
    const ac=new AbortController(); const t=setTimeout(()=>ac.abort(),timeout);
    try{
      const r=await fetch(url,Object.assign({cache:'no-store',signal:ac.signal},opt));
      clearTimeout(t);
      if(!r.ok) throw new Error('HTTP '+r.status);
      const j=await r.json(); setLink(true); return j;
    }catch(e){ clearTimeout(t); last=e; setLink(false); if(i+1<tries) await sleep(400*(i+1)); }
  }
  throw last;
}
const post=(url,data,tries=3)=>req(url,{method:'POST',body:new URLSearchParams(data)},tries);
function show(el,cls,text){ el.hidden=!text; el.className='msg out '+(cls||''); el.textContent=text||''; }
function busy(sec,on,text){
  sec.dataset.busy=on?'1':'';
  sec.querySelectorAll('button,input').forEach(b=>b.disabled=on);
  if(on){ const m=sec.querySelector('.out'); m.hidden=false; m.className='msg out busy';
    m.innerHTML='<span class=spin></span>'; m.append(text); }
}
)JS";
