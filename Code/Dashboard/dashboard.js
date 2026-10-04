"use strict";
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
