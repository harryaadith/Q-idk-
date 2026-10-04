#pragma once
const char DASHBOARD_HTML[] PROGMEM = R"TRAYPAGE(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Surgery Tray</title><style>
*{box-sizing:border-box}body{margin:0;background:#10191e;color:#edf6f6;font:16px system-ui}main{max-width:1100px;margin:auto;padding:30px}h1{margin-bottom:8px}.muted{color:#a2b8c0}#connection{padding:12px;background:#203038;border-radius:8px}#bricks{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-top:24px}article{background:#203038;padding:22px;border-radius:14px}article.offline{opacity:.45}.face-view{height:180px;display:grid;place-items:center}.square{position:relative;width:105px;height:105px;background:#386578;border:5px solid #608998;display:grid;place-items:center;transition:transform .3s}.face{position:absolute;background:#75878e}.face.occupied{background:#ffa33e}.n,.s{width:45px;height:9px;left:25px}.e,.w{height:45px;width:9px;top:25px}.n{top:-12px}.s{bottom:-12px}.e{right:-12px}.w{left:-12px}dl{display:grid;grid-template-columns:1fr 1fr;gap:8px}dd{margin:0;overflow-wrap:anywhere}button{padding:10px;background:#b5e2ea;color:#10232b;border:0;border-radius:8px;cursor:pointer}aside{margin-top:25px;padding:18px;border:1px solid #ffae58;border-radius:10px}a{color:#b5e2ea}</style>
<main><h1>Smart Surgery Tray</h1><p class="muted">Wireless brick telemetry · instruments are detected by top-facing RFID readers</p>
<p id="connection" role="status">Connecting to tray…</p><button id="calibrate">Calibrate gateway compass</button><p id="calibration" role="status"></p>
<aside><strong>Physical arrangement unresolved</strong><p>These cards show each brick’s orientation and occupied faces. Card positions do not represent the tray layout. IR occupancy does not identify the touching peer. Orange edges indicate detected objects; uncalibrated headings remain unknown.</p></aside>
<section id="bricks" aria-label="Brick telemetry"></section><p class="muted">Numbers are assigned from live hardware identities and may change when bricks join or leave. Compass heading assumes a flat table and aligned sensor mounting. RFID UID is the last scan, not proof an instrument is still present.</p></main><script src="/dashboard.js"></script></html>)TRAYPAGE";
const char DASHBOARD_JS[] PROGMEM = R"TRAYPAGE("use strict";
const cards = new Map();
const connection = document.getElementById("connection");
function render(state) {
  if (!state || !Array.isArray(state.bricks)) throw new Error("Invalid tray telemetry");
  const live = new Set();
  for (const b of [...state.bricks].sort((a,c)=>a.number-c.number)) {
    if (!/^[0-9A-F]{12,16}$/.test(b.uid)) continue;
    live.add(b.uid);
    let card = cards.get(b.uid);
    if (!card) {
      card = document.createElement("article");
      card.innerHTML = `<h2></h2><small class="uid"></small><div class="face-view"><div class="square"><span>N ↑</span><i class="face n"></i><i class="face e"></i><i class="face s"></i><i class="face w"></i></div></div><dl><dt>Heading</dt><dd class="heading"></dd><dt>Occupied faces</dt><dd class="faces"></dd><dt>Last instrument UID</dt><dd class="instrument"></dd><dt>Scan</dt><dd class="scan"></dd></dl>`;
      cards.set(b.uid,card);
    }
    card.classList.remove("offline");
    card.querySelector("h2").textContent = `Brick ${b.number}${b.gateway ? " · gateway" : ""}`;
    card.querySelector(".uid").textContent = b.uid;
    const valid = typeof b.heading === "number" && Number.isFinite(b.heading);
    card.querySelector(".square").style.transform = `rotate(${valid ? b.heading : 0}deg)`;
    card.querySelector(".heading").textContent = valid ? `${b.heading.toFixed(1)}° magnetic` : "Unknown / calibrate";
    const names=["N","E","S","W"];
    [...card.querySelectorAll(".face")].forEach((el,i)=>el.classList.toggle("occupied",!!(b.faces & (1<<i))));
    card.querySelector(".faces").textContent = names.filter((_,i)=>b.faces&(1<<i)).join(", ") || "None";
    card.querySelector(".instrument").textContent = b.instrumentUid || "No scan yet";
    card.querySelector(".scan").textContent = b.recentScan ? "Just scanned" : "No recent scan";
    document.getElementById("bricks").append(card);
  }
  for (const [uid,card] of cards) if (!live.has(uid)) {
    card.classList.add("offline"); card.querySelector(".scan").textContent="Offline / stale";
  }
  connection.textContent = `Connected · ${live.size} bricks online · physical layout unresolved`;
}
async function poll() {
  try {
    const response=await fetch("/api/state",{cache:"no-store",signal:AbortSignal.timeout(2500)});
    if(!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch(error) {
    connection.textContent="Tray disconnected. Check Wi-Fi; a gateway change requires reconnecting to SmartSurgeryTray.";
    for(const card of cards.values()) card.classList.add("offline");
  } finally { setTimeout(poll,500); }
}
document.getElementById("calibrate").onclick=async()=>{
  const output=document.getElementById("calibration");
  try {
    const response=await fetch("/api/calibrate",{method:"POST",signal:AbortSignal.timeout(3000)});
    output.textContent=await response.text();
  } catch(error) { output.textContent="Cannot reach the gateway."; }
};
poll();
)TRAYPAGE";
