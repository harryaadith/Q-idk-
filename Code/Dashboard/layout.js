"use strict";
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
