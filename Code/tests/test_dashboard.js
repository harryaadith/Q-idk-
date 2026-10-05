const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
function node(){return {textContent:'',style:{},attributes:{},disabled:false,value:'',classList:{add(){},remove(){},toggle(){}},children:[],append(x){this.children=this.children.filter(c=>c!==x);this.children.push(x);},replaceChildren(...children){this.children=children;},setAttribute(k,v){this.attributes[k]=v;},querySelector(s){return this.parts[s]??=node();},querySelectorAll(){return this.faces??=Array.from({length:4},node);},parts:{}};}
const nodes={};for(const id of ['connection','calibrate','calibration','bricks','candidate','confirm-layout','clear-confirmation','layout-status','tray-map','instrument-status']) nodes[id]=node();
const context={document:{getElementById:id=>nodes[id],createElement:node,createElementNS:node},fetch:()=>new Promise(()=>{}),AbortSignal,setTimeout(){},console};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../Dashboard/layout.js'),'utf8'),context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../Dashboard/dashboard.js'),'utf8'),context);
function brick(id,faces,heading=0){return {uid:String(id).padStart(12,'0'),number:id,faces,heading,orientationState:2,orientationSource:'gyro-relative',ageMs:0,instrumentUid:'',recentScan:false};}
const grid=[brick(1,6),brick(2,12),brick(3,3),brick(4,9)];grid[0].gateway=true;
context.render({bricks:grid});
assert(nodes.connection.textContent.includes('4 bricks'));
assert(nodes.bricks.children[0].parts.h2.textContent.includes('gateway'));
assert(nodes['layout-status'].textContent.includes('IR-based layout'));
assert.equal(nodes['tray-map'].children.length,4);
assert.equal(nodes['tray-map'].attributes.viewBox,'-20 -20 260 260');
const originalPositions=nodes['tray-map'].children.map(n=>n.children[0].attributes.x);
context.render({bricks:grid.map(n=>({...n,number:n.number+1,instrumentUid:'ABC'}))});
assert.deepEqual(nodes['tray-map'].children.map(n=>n.children[0].attributes.x),originalPositions);
const strip=[brick(1,2),brick(2,10),brick(3,10),brick(4,8)];
context.render({bricks:strip});
assert(nodes['layout-status'].textContent.includes('one of 2'));
assert.equal(nodes['tray-map'].children.length,4);
assert(nodes['tray-map'].children.every(n=>n.attributes['data-placement']==='inferred'));
const first=nodes['tray-map'].children.map(n=>[n.attributes['data-uid'],n.children[0].attributes.x]);
context.render({bricks:[...strip].reverse()});
assert.deepEqual(nodes['tray-map'].children.map(n=>[n.attributes['data-uid'],n.children[0].attributes.x]),first);
context.render({bricks:strip.slice(0,3)});
assert.equal(nodes['tray-map'].children.length,3);
assert(nodes['tray-map'].children.every(n=>n.attributes['data-placement']==='unplaced'));
context.render({bricks:grid.map(n=>({...n,heading:null,orientationState:1}))});
assert(nodes['layout-status'].textContent.includes('IR-based'));assert.equal(nodes['tray-map'].children.length,4);
assert(nodes['tray-map'].children.every(n=>n.attributes['data-placement']==='inferred'));
context.render({bricks:[{...brick(1,1),heading:null,orientationState:0},{...brick(2,2),heading:null,orientationState:0}]});
assert.equal(nodes['tray-map'].children.length,2);
assert(nodes['tray-map'].children.every(n=>n.attributes['data-placement']==='inferred'));
assert(nodes.bricks.children.at(-1).parts['.heading'].textContent.includes('Unknown'));
context.render({bricks:[brick(1,0,90)]});
assert.equal(nodes['tray-map'].children.length,1);
assert.equal(nodes.bricks.children.at(-1).parts['.square'].style.transform,'rotate(90deg)');
const before=nodes.bricks.children.length;context.render({bricks:[{uid:'<script>',number:3}]});
assert.equal(nodes.bricks.children.length,before);assert.equal(nodes['tray-map'].children.length,0);
assert(nodes.bricks.children[0].parts['.scan'].textContent.includes('Offline'));
console.log('PASS: SVG positions, relative orientation, automatic ambiguous layout, deterministic selection, unplaced fallback, stale/missing readings, UID renumbering and invalid identities.');

// Completion uses persistent per-brick UID records, not the brief recent-scan flag.
const scans=[{...brick(1,1),instrumentUid:'AABBCCDD',recentScan:false},brick(2,4)];
context.render({bricks:scans});
assert(nodes['instrument-status'].textContent.includes('1 of 2'));
assert.equal(nodes['instrument-status'].attributes['data-state'],'incomplete');
scans[1].instrumentUid='11223344';context.render({bricks:scans});
assert(nodes['instrument-status'].textContent.includes('All instruments placed'));
assert.equal(nodes['instrument-status'].attributes['data-state'],'complete');
context.render({bricks:[...scans,brick(3,0)]});
assert(nodes['instrument-status'].textContent.includes('2 of 3'));
context.render({bricks:scans.map(b=>({...b,ageMs:1001}))});
assert.equal(nodes['instrument-status'].attributes['data-state'],'unavailable');
context.render({bricks:[scans[0],{...scans[1],instrumentUid:''}]}); // Reboot clears scan.
assert.equal(nodes['instrument-status'].attributes['data-state'],'incomplete');
context.render({bricks:[]});assert.equal(nodes['instrument-status'].attributes['data-state'],'unavailable');
context.render({bricks:[{...brick(1,0),instrumentUid:'<script>'}]});
assert.equal(nodes['instrument-status'].attributes['data-state'],'incomplete');
context.fetch=async()=>{throw Error('connection lost');};
(async()=>{
 context.render({bricks:scans});assert.equal(nodes['instrument-status'].attributes['data-state'],'complete');
 await context.poll();assert.equal(nodes['instrument-status'].attributes['data-state'],'unavailable');
 assert(nodes['instrument-status'].textContent.includes('telemetry unavailable'));
 console.log('PASS: scan completion, expired recent-scan flag, new brick, reboot, stale readings, empty group and disconnect.');
})().catch(error=>{console.error(error);process.exitCode=1;});
