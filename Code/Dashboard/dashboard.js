"use strict";
const cards = new Map();
const connection = document.getElementById("connection");
let lastBricks=[], layoutResult=null, layoutFingerprint=null, candidateIndex=0, confirmed=false;
function svgNode(tag,attributes,text) {
  const el=document.createElementNS("http://www.w3.org/2000/svg",tag);
  for(const [name,value] of Object.entries(attributes)) el.setAttribute(name,String(value));
  if(text!==undefined) el.textContent=text;
  return el;
}
function drawLayout() {
  const svg=document.getElementById("tray-map");svg.replaceChildren();
  const layout=layoutResult?.layouts[candidateIndex];
  if(!layout) return;
  const minX=Math.min(...layout.map(n=>n.x)),maxX=Math.max(...layout.map(n=>n.x));
  const minY=Math.min(...layout.map(n=>n.y)),maxY=Math.max(...layout.map(n=>n.y));
  const width=(maxX-minX+1)*110,height=(maxY-minY+1)*110;
  svg.setAttribute("viewBox",`-20 -20 ${width+40} ${height+40}`);
  for(const n of layout) {
    const live=lastBricks.find(b=>b.uid===n.uid) || n;
    const x=(n.x-minX)*110,y=(n.y-minY)*110;
    const group=svgNode("g",{"data-uid":n.uid});
    group.append(svgNode("rect",{x,y,width:110,height:110,rx:6,class:"map-cell"}));
    for(let d=0;d<4;d++) if(n.worldMask&(1<<d)) {
      const edges=[[x+35,y+3,x+75,y+3],[x+107,y+35,x+107,y+75],
                   [x+35,y+107,x+75,y+107],[x+3,y+35,x+3,y+75]];
      const [x1,y1,x2,y2]=edges[d];group.append(svgNode("line",{x1,y1,x2,y2,class:"map-face"}));
    }
    group.append(svgNode("text",{x:x+55,y:y+50,class:"map-label"},`Brick ${live.number}`));
    group.append(svgNode("text",{x:x+55,y:y+69,class:"map-label"},live.instrumentUid ? `Tag …${live.instrumentUid.slice(-6)}` : "No tag scan"));
    group.append(svgNode("text",{x:x+55,y:y+26,class:"map-arrow",transform:`rotate(${n.turns*90},${x+55},${y+55})`},"N ↑"));
    group.append(svgNode("title",{},`UID ${n.uid}; inferred grid (${n.x}, ${n.y}); relative yaw ${live.heading}°; last instrument UID ${live.instrumentUid || "none"}`));
    svg.append(group);
  }
}
function updateLayoutStatus() {
  const output=document.getElementById("layout-status"), result=layoutResult;
  if(!result?.layouts.length) output.textContent=result?.reason || "Waiting for telemetry.";
  else if(confirmed) output.textContent=`Candidate ${candidateIndex+1} confirmed by you for current telemetry; this is not a measured face identity.`;
  else if(result.status==="unique") output.textContent="One inferred layout fits the readings under the connected-grid and alignment assumptions.";
  else output.textContent=`${result.layouts.length}${result.truncated ? "+" : ""} possible layouts. Preview alternatives and confirm only after comparing with the physical tray.${result.truncated ? " Search was limited; uniqueness is not established." : ""}`;
  document.getElementById("confirm-layout").disabled=!result?.layouts.length || confirmed;
  document.getElementById("clear-confirmation").disabled=!confirmed;
}
function updateLayout(bricks) {
  lastBricks=bricks;layoutResult=TrayLayout.solve(bricks);
  const fingerprint=layoutResult.fingerprint ?? `invalid:${layoutResult.status}:${layoutResult.reason}`;
  if(fingerprint!==layoutFingerprint) {
    layoutFingerprint=fingerprint;confirmed=false;candidateIndex=0;
    const select=document.getElementById("candidate");select.replaceChildren();
    layoutResult.layouts.forEach((_,i)=>{
      const option=document.createElement("option");option.value=String(i);option.textContent=`Candidate ${i+1}`;select.append(option);
    });
    select.value="0";select.disabled=layoutResult.layouts.length<2;
  }
  updateLayoutStatus();drawLayout();
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
    layoutFingerprint=null;confirmed=false;layoutResult=null;document.getElementById("candidate").replaceChildren();document.getElementById("candidate").disabled=true;updateLayoutStatus();drawLayout();
  } finally { setTimeout(poll,500); }
}
document.getElementById("calibrate").onclick=async()=>{
  const output=document.getElementById("calibration");
  try {
    const response=await fetch("/api/align",{method:"POST",signal:AbortSignal.timeout(3000)});
    output.textContent=await response.text();
    if(response.ok) {layoutFingerprint=null;confirmed=false;layoutResult=null;document.getElementById("candidate").replaceChildren();document.getElementById("candidate").disabled=true;updateLayoutStatus();drawLayout();}
  } catch(error) { output.textContent="Cannot reach the gateway."; }
};
document.getElementById("candidate").onchange=event=>{
  const index=Number(event.target.value);
  if(!Number.isInteger(index) || !layoutResult?.layouts[index]) return;
  candidateIndex=index;confirmed=false;updateLayoutStatus();drawLayout();
};
document.getElementById("confirm-layout").onclick=()=>{if(layoutResult?.layouts[candidateIndex]) {confirmed=true;updateLayoutStatus();}};
document.getElementById("clear-confirmation").onclick=()=>{confirmed=false;updateLayoutStatus();};
poll();
