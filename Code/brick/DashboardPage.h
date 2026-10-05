#pragma once
const char DASHBOARD_HTML[] PROGMEM = R"TRAYPAGE(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Surgery Tray</title><style>
*{box-sizing:border-box}body{margin:0;background:#10191e;color:#edf6f6;font:16px system-ui}main{max-width:1100px;margin:auto;padding:30px}h1{margin-bottom:8px}.muted{color:#a2b8c0}#connection{padding:12px;background:#203038;border-radius:8px}#bricks{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-top:24px}article{background:#203038;padding:22px;border-radius:14px}article.offline{opacity:.45}.face-view{height:180px;display:grid;place-items:center}.square{position:relative;width:105px;height:105px;background:#386578;border:5px solid #608998;display:grid;place-items:center;transition:transform .3s}.face{position:absolute;background:#75878e}.face.occupied{background:#ffa33e}.n,.s{width:45px;height:9px;left:25px}.e,.w{height:45px;width:9px;top:25px}.n{top:-12px}.s{bottom:-12px}.e{right:-12px}.w{left:-12px}dl{display:grid;grid-template-columns:1fr 1fr;gap:8px}dd{margin:0;overflow-wrap:anywhere}select{padding:10px;background:#203038;color:#edf6f6;border:1px solid #608998;border-radius:8px}button:disabled{opacity:.4;cursor:default}button{padding:10px;background:#b5e2ea;color:#10232b;border:0;border-radius:8px;cursor:pointer}aside{margin-top:25px;padding:18px;border:1px solid #ffae58;border-radius:10px}a{color:#b5e2ea}#tray-map{width:100%;height:360px;background:#17262e;border-radius:12px;margin-top:12px}#map-panel{margin-top:25px}.map-cell{fill:#386578;stroke:#608998;stroke-width:2}.map-unplaced{fill:#203038;stroke-dasharray:6 4}.map-face{stroke:#ffa33e;stroke-width:5}.map-label{fill:#edf6f6;font-size:12px;text-anchor:middle}.map-arrow{fill:#b5e2ea;font-size:12px;text-anchor:middle}.controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center}button,select{font:inherit}h2{font-size:20px}</style>
<main><h1>Smart Surgery Tray</h1><p class="muted">Wireless brick telemetry · instruments are detected by top-facing RFID readers</p>
<p id="connection" role="status">Connecting to tray…</p><button id="calibrate">Realign all bricks / zero gyro yaw</button><p id="calibration" role="status"></p>
<aside><strong>IR-based 2D tray inference — no direction calibration needed</strong><p>The map uses occupied IR faces and ignores gyro directions. It automatically draws one possible connected square-grid arrangement, inferring rotations for display. IR detections should be caused only by adjoining bricks; occupied faces do not identify touching peers.</p></aside>
<section id="map-panel" aria-label="Inferred tray configuration"><h2>2D tray configuration</h2><p id="layout-status" role="status">Waiting for IR telemetry…</p><svg id="tray-map" viewBox="0 0 640 360" role="img" aria-label="Inferred brick positions"></svg><p class="muted">Up is an arbitrary drawing direction. Arrows show inferred local N faces, not measured compass directions. When multiple layouts fit, one is displayed automatically and marked uncertain. Bricks with unresolved positions are shown separately with dashed outlines; their spacing is not physical.</p></section>
<section id="bricks" aria-label="Brick telemetry"></section><p class="muted">Numbers are assigned from live hardware identities and may change when bricks join or leave. Gyro yaw requires aligned startup, motion along the tray plane, periodic realignment and working accel/gyro. Tilting over 20° from the calibrated pose, sample gaps or saturation can invalidate tracking. RFID UID is the last scan, not proof an instrument is still present.</p></main><script src="/layout.js"></script><script src="/dashboard.js"></script></html>)TRAYPAGE";
const char DASHBOARD_JS[] PROGMEM = R"TRAYPAGE("use strict";
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
)TRAYPAGE";
const char LAYOUT_JS[] PROGMEM = R"TRAYPAGE("use strict";
(function (root) {
  const directions = [[0,-1],[1,0],[0,1],[-1,0]];
  function rotateMask(mask, turns) {
    let result=0;
    for(let face=0;face<4;face++) if(mask & (1<<face)) result |= 1<<((face+turns)%4);
    return result;
  }
  function solve(bricks, limits={}) {
    const ignoreOrientation=limits.ignoreOrientation === true;
    const maxStates=limits.maxStates ?? 75000, maxLayouts=limits.maxLayouts ?? 64;
    if(!bricks.length) return {status:"waiting",reason:"No bricks online.",layouts:[]};
    if(bricks.length>4) return {status:"invalid",reason:"This solver supports the intended four-brick tray.",layouts:[]};
    if(bricks.some(b=>!b || typeof b.uid!=="string")) return {status:"invalid",reason:"Invalid brick identity.",layouts:[]};
    const seen=new Set(), nodes=[];
    for(const brick of [...bricks].sort((a,b)=>a.uid.localeCompare(b.uid))) {
      if(!/^[0-9A-F]{12,16}$/.test(brick.uid) || seen.has(brick.uid) ||
         !Number.isInteger(brick.faces) || brick.faces<0 || brick.faces>15) {
        return {status:"invalid",reason:"Invalid or duplicate brick telemetry.",layouts:[]};
      }
      seen.add(brick.uid);
      if(ignoreOrientation) {
        if(!Number.isFinite(brick.ageMs) || brick.ageMs<0 || brick.ageMs>1000)
          return {status:"waiting",reason:`Brick ${brick.number}: waiting for fresh IR readings.`,layouts:[]};
        nodes.push({...brick,turns:0,worldMask:brick.faces});
        continue;
      }
      if(brick.orientationSource!=="gyro-relative" || brick.orientationState!==2 ||
         typeof brick.heading!=="number" || !Number.isFinite(brick.heading) ||
         !Number.isFinite(brick.ageMs) || brick.ageMs<0 || brick.ageMs>1000) {
        return {status:"waiting",reason:`Brick ${brick.number}: waiting for fresh aligned gyro orientation. Align all N marks and keep still; realign after tracking loss.`,layouts:[]};
      }
      const heading=((brick.heading%360)+360)%360;
      const turns=Math.round(heading/90)%4;
      const difference=Math.abs(((heading-turns*90+540)%360)-180);
      if(difference>15) return {status:"waiting",reason:`Brick ${brick.number} is between 90° positions, or yaw has drifted. Dock squarely or realign.`,layouts:[]};
      nodes.push({...brick,turns,worldMask:rotateMask(brick.faces,turns)});
    }
    const fingerprint=nodes.map(n=>`${n.uid}:${n.faces}:${n.turns}`).join("|");
    const placements=new Map([[nodes[0].uid,{...nodes[0],x:0,y:0}]]);
    const cells=new Map([["0,0",nodes[0].uid]]);
    const layouts=[], unique=new Set(); let explored=0,truncated=false;
    function canPlace(node,x,y) {
      for(let d=0;d<4;d++) {
        const [dx,dy]=directions[d], uid=cells.get(`${x+dx},${y+dy}`);
        if(uid) {
          const peer=placements.get(uid);
          if(!(node.worldMask&(1<<d)) || !(peer.worldMask&(1<<((d+2)%4)))) return false;
        }
      }
      return true;
    }
    function search() {
      if(++explored>maxStates || layouts.length>=maxLayouts) {truncated=true;return;}
      let target=null;
      for(const node of placements.values()) {
        for(let d=0;d<4;d++) if(node.worldMask&(1<<d)) {
          const [dx,dy]=directions[d],x=node.x+dx,y=node.y+dy;
          const uid=cells.get(`${x},${y}`);
          if(uid) {
            if(!(placements.get(uid).worldMask&(1<<((d+2)%4)))) return;
          } else if(!target) target={x,y,opposite:(d+2)%4};
        }
      }
      if(!target) {
        if(placements.size!==nodes.length) return;
        const layout=[...placements.values()].sort((a,b)=>a.uid.localeCompare(b.uid));
        const key=layout.map(n=>`${n.uid}:${n.x},${n.y}`).join("|");
        if(!unique.has(key)) {unique.add(key);layouts.push(layout.map(n=>({...n})));}
        return;
      }
      for(const original of nodes) {
        if(placements.has(original.uid)) continue;
        for(let turn=0;turn<(ignoreOrientation ? 4 : 1);turn++) {
          const node=ignoreOrientation ? {...original,turns:turn,worldMask:rotateMask(original.faces,turn)} : original;
          if(!(node.worldMask&(1<<target.opposite)) || !canPlace(node,target.x,target.y)) continue;
          placements.set(node.uid,{...node,x:target.x,y:target.y});cells.set(`${target.x},${target.y}`,node.uid);
          search();placements.delete(node.uid);cells.delete(`${target.x},${target.y}`);
          if(truncated) return;
        }
      }
    }
    search();
    let status=truncated ? "limited" : layouts.length===0 ? "inconsistent" : layouts.length===1 ? "unique" : "ambiguous";
    return {status,layouts,truncated,explored,fingerprint,reason:layouts.length ? "" :
      (ignoreOrientation ? "No connected grid fits these IR readings. Check missing bricks, false detections or face wiring." : "No connected grid fits these readings. Check missing bricks, false IR detections, face wiring, alignment and drift.")};
  }
  root.TrayLayout={solve,rotateMask,directions};
  if(typeof module!=="undefined" && module.exports) module.exports=root.TrayLayout;
})(globalThis);
)TRAYPAGE";
