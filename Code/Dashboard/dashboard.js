"use strict";
const cards = new Map();
const connection = document.getElementById("connection");
let lastBricks=[], layoutResult=null;
function svgNode(tag,attributes,text) {
  const el=document.createElementNS("http://www.w3.org/2000/svg",tag);
  for(const [name,value] of Object.entries(attributes)) el.setAttribute(name,String(value));
  if(text!==undefined) el.textContent=text;
  return el;
}
function drawLayout() {
  const svg=document.getElementById("tray-map");svg.replaceChildren();
  const inferred=!!layoutResult?.layouts.length;
  const layout=inferred ? layoutResult.layouts[0] :
    [...lastBricks].sort((a,b)=>a.uid.localeCompare(b.uid)).map((brick,i)=>({
      ...brick,x:(i%4)*1.3,y:Math.floor(i/4)*1.3,worldMask:0
    }));
  if(!layout.length) return;
  const minX=Math.min(...layout.map(n=>n.x)),maxX=Math.max(...layout.map(n=>n.x));
  const minY=Math.min(...layout.map(n=>n.y)),maxY=Math.max(...layout.map(n=>n.y));
  const width=(maxX-minX+1)*110,height=(maxY-minY+1)*110;
  svg.setAttribute("viewBox",`-20 -20 ${width+40} ${height+40}`);
  for(const n of layout) {
    const live=lastBricks.find(b=>b.uid===n.uid) || n;
    const x=(n.x-minX)*110,y=(n.y-minY)*110;
    const group=svgNode("g",{"data-uid":n.uid,"data-placement":inferred ? "inferred" : "unplaced"});
    group.append(svgNode("rect",{x,y,width:110,height:110,rx:6,class:inferred ? "map-cell" : "map-cell map-unplaced"}));
    for(let d=0;d<4;d++) if(n.worldMask&(1<<d)) {
      const edges=[[x+35,y+3,x+75,y+3],[x+107,y+35,x+107,y+75],
                   [x+35,y+107,x+75,y+107],[x+3,y+35,x+3,y+75]];
      const [x1,y1,x2,y2]=edges[d];group.append(svgNode("line",{x1,y1,x2,y2,class:"map-face"}));
    }
    group.append(svgNode("text",{x:x+55,y:y+50,class:"map-label"},`Brick ${live.number}`));
    group.append(svgNode("text",{x:x+55,y:y+69,class:"map-label"},!inferred ? "Position unknown" : live.instrumentUid ? `Tag …${live.instrumentUid.slice(-6)}` : "No tag scan"));
    if(inferred) group.append(svgNode("text",{x:x+55,y:y+26,class:"map-arrow",transform:`rotate(${n.turns*90},${x+55},${y+55})`},"N ↑"));
    group.append(svgNode("title",{},inferred ? `UID ${n.uid}; inferred grid (${n.x}, ${n.y}); drawing orientation inferred from IR; last instrument UID ${live.instrumentUid || "none"}` : `UID ${n.uid}; position unresolved; spacing is for display only`));
    svg.append(group);
  }
}
function updateLayoutStatus() {
  const output=document.getElementById("layout-status"), result=layoutResult;
  if(!result?.layouts.length) output.textContent=(result?.reason || "Waiting for telemetry.") +
    (lastBricks.length ? " Bricks are shown unplaced; spacing does not represent the physical tray." : "");
  else if(result.status==="unique") output.textContent="One possible IR-based layout is displayed. Directions are inferred for drawing; gyro orientation is ignored.";
  else output.textContent=`Automatically showing one of ${result.layouts.length}${result.truncated ? "+" : ""} IR-based layouts. Gyro directions are ignored; actual brick positions are uncertain.${result.truncated ? " Search was limited." : ""}`;
}
function updateLayout(bricks) {
  lastBricks=bricks;layoutResult=TrayLayout.solve(bricks,{ignoreOrientation:true});
  updateLayoutStatus();drawLayout();
}
function updateInstrumentStatus(bricks=null) {
  const output=document.getElementById("instrument-status");
  if(!bricks || !bricks.length) {
    output.setAttribute("data-state","unavailable");
    output.textContent=bricks ? "Waiting for online bricks and instrument scans." : "Instrument placement unconfirmed — telemetry unavailable.";
    return;
  }
  if(bricks.some(b=>!Number.isFinite(b.ageMs) || b.ageMs<0 || b.ageMs>1000)) {
    output.setAttribute("data-state","unavailable");
    output.textContent="Instrument placement unconfirmed — some brick readings are stale.";
    return;
  }
  const scanned=bricks.filter(b=>typeof b.instrumentUid==="string" &&
    /^[0-9A-F]{8,20}$/.test(b.instrumentUid) && b.instrumentUid.length%2===0).length;
  output.setAttribute("data-state",scanned===bricks.length ? "complete" : "incomplete");
  output.textContent=scanned===bricks.length ?
    `All instruments placed — RFID scans recorded on all ${bricks.length} online bricks.` :
    `Waiting for instruments — ${scanned} of ${bricks.length} online bricks have scanned an RFID card.`;
}
function render(state) {
  if (!state || !Array.isArray(state.bricks)) throw new Error("Invalid tray telemetry");
  const live = new Set(), current=[];
  for (const b of [...state.bricks].sort((a,c)=>a.number-c.number)) {
    if (!/^[0-9A-F]{12,16}$/.test(b.uid)) continue;
    live.add(b.uid);current.push(b);
    let card = cards.get(b.uid);
    if (!card) {
      card = document.createElement("article");
      card.innerHTML = `<h2></h2><small class="uid"></small><div class="face-view"><div class="square"><span>N ↑</span><i class="face n"></i><i class="face e"></i><i class="face s"></i><i class="face w"></i></div></div><dl><dt>Relative yaw</dt><dd class="heading"></dd><dt>Orientation</dt><dd class="orientation"></dd><dt>Occupied faces</dt><dd class="faces"></dd><dt>Last instrument UID</dt><dd class="instrument"></dd><dt>Scan</dt><dd class="scan"></dd></dl>`;
      cards.set(b.uid,card);
    }
    card.classList.remove("offline");
    card.querySelector("h2").textContent = `Brick ${b.number}${b.gateway ? " · gateway" : ""}`;
    card.querySelector(".uid").textContent = b.uid;
    const valid=b.orientationSource==="gyro-relative" && b.orientationState===2 &&
      typeof b.heading==="number" && Number.isFinite(b.heading) && b.ageMs<=1000;
    card.querySelector(".square").style.transform = `rotate(${valid ? b.heading : 0}deg)`;
    card.querySelector(".heading").textContent = valid ? `${b.heading.toFixed(1)}° from aligned start` : "Unknown / realign";
    const states=["IMU unavailable","Keep still for gyro bias calibration","Tracking; relative yaw drifts","Tracking lost; align all bricks again"];
    card.querySelector(".orientation").textContent=b.orientationState===2 && !valid ? "Samples unavailable/stale; check IMU and realign" : (states[b.orientationState] || "Firmware orientation unavailable") + (valid && Number.isFinite(b.alignmentAgeMs) ? ` · reference ${Math.floor(b.alignmentAgeMs/1000)} s old` : "");
    const names=["N","E","S","W"];
    [...card.querySelectorAll(".face")].forEach((el,i)=>el.classList.toggle("occupied",!!(b.faces & (1<<i))));
    card.querySelector(".faces").textContent = names.filter((_,i)=>b.faces&(1<<i)).join(", ") || "None";
    card.querySelector(".instrument").textContent = b.instrumentUid || "No scan yet";
    card.querySelector(".scan").textContent = b.recentScan ? "Just scanned" : "No recent scan";
    document.getElementById("bricks").append(card);
  }
  for (const [uid,card] of cards) if (!live.has(uid)) {
    card.classList.add("offline");card.querySelector(".scan").textContent="Offline / stale";
  }
  updateInstrumentStatus(current);
  updateLayout(current);
  connection.textContent=`Connected · ${live.size} bricks online · ${layoutResult.status} layout inference`;
}
async function poll() {
  try {
    const response=await fetch("/api/state",{cache:"no-store",signal:AbortSignal.timeout(2500)});
    if(!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch(error) {
    connection.textContent="Tray disconnected. Check Wi-Fi; a gateway change requires reconnecting to SmartSurgeryTray.";
    for(const card of cards.values()) card.classList.add("offline");
    updateInstrumentStatus();
    layoutResult={layouts:[],reason:"Disconnected; last seen bricks have no current position readings."};updateLayoutStatus();drawLayout();
  } finally { setTimeout(poll,500); }
}
document.getElementById("calibrate").onclick=async()=>{
  const output=document.getElementById("calibration");
  try {
    const response=await fetch("/api/align",{method:"POST",signal:AbortSignal.timeout(3000)});
    output.textContent=await response.text();
    // Gyro realignment does not change the IR-only map.
  } catch(error) { output.textContent="Cannot reach the gateway."; }
};
poll();
