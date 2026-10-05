const assert=require('assert');
const {solve,rotateMask}=require('../Dashboard/layout.js');
function brick(id,faces,heading=0) {
  return {uid:String(id).padStart(12,'0'),number:id,faces,heading,orientationSource:'gyro-relative',orientationState:2,ageMs:0};
}
const grid=[brick(1,6),brick(2,12),brick(3,3),brick(4,9)];
let result=solve(grid);assert.equal(result.status,'unique');
assert.deepEqual(result.layouts[0].map(n=>[n.x,n.y]),[[0,0],[1,0],[0,1],[1,1]]);
const rotated=grid.map((n,i)=>({...n,heading:i*90,faces:rotateMask(n.faces,(4-i)%4)}));
result=solve(rotated);assert.equal(result.status,'unique');
assert.deepEqual(result.layouts[0].map(n=>[n.x,n.y]),[[0,0],[1,0],[0,1],[1,1]]);
const strip=[brick(1,2),brick(2,10),brick(3,10),brick(4,8)];
result=solve(strip);assert.equal(result.status,'ambiguous');assert.equal(result.layouts.length,2);
assert.notDeepEqual(result.layouts[0].map(n=>n.x),result.layouts[1].map(n=>n.x));
assert.equal(solve([brick(1,6),brick(2,10),brick(3,8),brick(4,1)]).status,'unique');
assert.equal(solve([brick(1,0),brick(2,0)]).status,'inconsistent');
assert.equal(solve([brick(1,2),brick(2,1)]).status,'inconsistent');
assert.equal(solve([{...grid[0],heading:null},...grid.slice(1)]).status,'waiting');
assert.equal(solve([{...grid[0],heading:45},...grid.slice(1)]).status,'waiting');
assert.equal(solve([{...grid[0],heading:359},...grid.slice(1)]).status,'unique');
assert.equal(solve([{...grid[0],orientationState:3},...grid.slice(1)]).status,'waiting');
assert.equal(solve([{...grid[0],ageMs:1001},...grid.slice(1)]).status,'waiting');
assert.equal(solve([{...grid[0],heading:NaN},...grid.slice(1)]).status,'waiting');
assert.equal(solve([grid[0],grid[0]]).status,'invalid');
assert.equal(solve([brick(1,16)]).status,'invalid');
assert.equal(solve(strip,{maxStates:1}).status,'limited');
assert.equal(solve(strip,{maxLayouts:1}).status,'limited');
assert.equal(solve([brick(1,0)]).status,'unique');
// Independent brute-force embedding oracle for four bricks, including missing/extra IR edges.
function oracle(bricks) {
  const coords=[[0,0]], answers=new Set(), dirs=[[0,-1],[1,0],[0,1],[-1,0]];
  function recurse(i) {
    if(i===bricks.length) {
      const graph=bricks.map(()=>[]);
      for(let a=0;a<bricks.length;a++) {
        let actual=0;
        for(let b=0;b<bricks.length;b++) if(a!==b) {
          const d=dirs.findIndex(([dx,dy])=>coords[b][0]-coords[a][0]===dx && coords[b][1]-coords[a][1]===dy);
          if(d>=0) {actual|=1<<d;graph[a].push(b);}
        }
        if(actual!==bricks[a].faces) return;
      }
      const visited=new Set([0]),todo=[0];
      while(todo.length) for(const n of graph[todo.pop()]) if(!visited.has(n)){visited.add(n);todo.push(n);}
      if(visited.size===bricks.length) answers.add(coords.map(p=>p.join(',')).join('|'));
      return;
    }
    for(let x=-3;x<=3;x++) for(let y=-3;y<=3;y++) {
      if(coords.some(p=>p[0]===x&&p[1]===y)) continue;
      coords.push([x,y]);recurse(i+1);coords.pop();
    }
  }
  recurse(1);return answers;
}
for(const sample of [grid,strip,[brick(1,6),brick(2,10),brick(3,8),brick(4,1)],
  [brick(1,2),brick(2,10),brick(3,10),brick(4,9)]]) {
 const inferred=new Set(solve(sample).layouts.map(l=>l.map(n=>`${n.x},${n.y}`).join('|')));
 assert.deepEqual(inferred,oracle(sample));
}
console.log('PASS: rotated grid, strip ambiguity, L-shape, contradictory/disconnected data, freshness, bounds, and independent exhaustive embedding oracle.');

// IR-only mapping: all face pairs fit without ready gyro, even non-opposite local labels.
for(const first of [1,2,4,8]) for(const second of [1,2,4,8]) {
 const data=[{...brick(1,first),heading:null,orientationState:0},{...brick(2,second),heading:47,orientationState:1}];
 const result=solve(data,{ignoreOrientation:true});
 assert.equal(result.status,'unique');assert.equal(result.layouts[0].length,2);
 const [a,b]=result.layouts[0];
 assert.equal(Math.abs(a.x-b.x)+Math.abs(a.y-b.y),1);
}
assert.equal(solve([{...brick(1,1),ageMs:1001},brick(2,4)],{ignoreOrientation:true}).status,'waiting');
console.log('PASS: every two-brick face pair fits without gyro orientation; stale IR data is rejected.');
