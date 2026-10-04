const fs=require('fs'),vm=require('vm'),assert=require('assert');
function node(){return {textContent:'',style:{},classList:{add(){},remove(){},toggle(){}},children:[],append(x){this.children=this.children.filter(c=>c!==x);this.children.push(x);},querySelector(s){return this.parts[s]??=(node());},querySelectorAll(){return this.faces??=Array.from({length:4},node);},parts:{}};}
const nodes={connection:node(),calibrate:node(),calibration:node(),bricks:node()};
const context={document:{getElementById:id=>nodes[id],createElement:node},fetch:()=>new Promise(()=>{}),AbortSignal,setTimeout(){},console};
vm.createContext(context);
vm.runInContext(fs.readFileSync(require('path').join(__dirname,'../Dashboard/dashboard.js'),'utf8'),context);
context.render({bricks:[{uid:'000000000002',number:2,heading:90,faces:1,instrumentUid:'1234',recentScan:true},{uid:'000000000001',number:1,gateway:true,heading:null,faces:0}]});
assert(nodes.connection.textContent.includes('2 bricks'));
assert(nodes.bricks.children[0].parts.h2.textContent.includes('gateway'));
assert(nodes.bricks.children[1].parts['.square'].style.transform==='rotate(90deg)');
context.render({bricks:[{uid:'000000000002',number:1,heading:null,faces:0,instrumentUid:''}]});
assert(nodes.bricks.children.length===2); // UID identity preserves card despite renumbering.
assert(nodes.connection.textContent.includes('1 bricks'));
assert(nodes.bricks.children[0].parts['.scan'].textContent.includes('Offline'));
assert(nodes.bricks.children[1].parts['.heading'].textContent.includes('Unknown'));
context.render({bricks:[{uid:'<script>',number:3}]});
assert(nodes.bricks.children.length===2);
console.log('PASS: telemetry rendering, orientation, unknown heading, renumbering, stale peers and invalid identities.');
