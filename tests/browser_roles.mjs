// Real browser tests through Edge's DevTools protocol; no npm dependencies.
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
const [origin,profile,artifacts]=process.argv.slice(2);
fs.mkdirSync(profile,{recursive:true});fs.mkdirSync(artifacts,{recursive:true});
const edge=process.env.LMS_EDGE_PATH || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const port=await new Promise(resolve=>{const s=net.createServer();s.listen(0,'127.0.0.1',()=>{const port=s.address().port;s.close(()=>resolve(port));});});
const log=fs.openSync(path.join(profile,'edge.log'),'w');
const browser=spawn(edge,['--headless=new','--disable-gpu','--disable-crash-reporter','--disable-background-networking','--disable-component-update','--disable-sync','--no-first-run','--no-default-browser-check',`--remote-debugging-port=${port}`,`--user-data-dir=${profile}`,`--disk-cache-dir=${path.join(profile,'cache')}`,'about:blank'],{stdio:['ignore',log,log],windowsHide:true});
let socket;let sequence=0;const waiting=new Map();const events=new Map();let checks=0;
async function send(method,params={}){
  const id=++sequence;
  return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{waiting.delete(id);reject(new Error('CDP timed out: '+method));},30000);waiting.set(id,{resolve,reject,timer});socket.send(JSON.stringify({id,method,params}));});
}
async function evaluate(expression){const value=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(value.exceptionDetails)throw new Error(value.exceptionDetails.exception?.description||value.exceptionDetails.text);return value.result.value;}
async function until(expression,message){for(let i=0;i<150;i++){if(await evaluate(expression))return;await sleep(100);}throw new Error(message);}
async function mouseClick(selector){
  const point=await evaluate(`(()=>{const el=[...document.querySelectorAll(${JSON.stringify(selector)})].find(el=>el.getBoundingClientRect().width>0);if(!el)throw Error('Missing click target');el.scrollIntoView({block:'center'});const r=el.getBoundingClientRect();const x=r.x+r.width/2,y=r.y+r.height/2;if(!el.contains(document.elementFromPoint(x,y)))throw Error('Click target covered: '+${JSON.stringify(selector)});return {x,y};})()`);
  await send('Input.dispatchMouseEvent',{type:'mousePressed',...point,button:'left',clickCount:1});
  await send('Input.dispatchMouseEvent',{type:'mouseReleased',...point,button:'left',clickCount:1});
}
async function login(name){
  if(socket)await evaluate(`if(location.origin===${JSON.stringify(origin)})localStorage.clear()`);
  await send('Page.navigate',{url:origin+'/?test='+Date.now()});
  await until("document.readyState==='complete' && typeof doLogin==='function'",'Page not ready');
  await evaluate(`document.getElementById('loginUser').value=${JSON.stringify(name)};document.getElementById('loginPass').value=${JSON.stringify(name+'123')}`);
  await mouseClick('#loginBtn');
  await until("!document.getElementById('mainPage').classList.contains('hidden')",'Login did not complete: '+name);
  await until("!document.getElementById('loginBtn').disabled",'Workspace did not finish loading: '+name);
}
async function status(id,expected){await until(`api('GET','/api/orders/${id}').then(d=>d.order.status_id===${expected})`,'Order did not reach '+expected);}
async function clickAction(id,action){
  await until(`Array.from(document.querySelectorAll('tr')).some(row=>row.textContent.includes('${id}') && row.querySelector('[data-action="${action}"]')?.getBoundingClientRect().width>0)`,'Missing visible action '+action);
  await mouseClick(`tr[data-order-id="${id}"] [data-action="${action}"]`);
  await until("document.getElementById('confirmModal').classList.contains('show')",'Confirmation not shown');
  await mouseClick('#confirmApproveBtn');
}
try{
  let target;
  for(let i=0;i<150;i++){try{target=await(await fetch(`http://127.0.0.1:${port}/json/new?about:blank`,{method:'PUT'})).json();break;}catch{await sleep(100);}}
  if(!target)throw new Error('Edge DevTools unavailable');
  socket=new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
  socket.addEventListener('message',async event=>{const message=JSON.parse(event.data);if(message.id){const request=waiting.get(message.id);if(request){clearTimeout(request.timer);waiting.delete(message.id);message.error?request.reject(new Error(message.error.message)):request.resolve(message.result);}}else{for(const handler of events.get(message.method)||[])handler(message.params);}});
  await send('Page.enable');await send('Runtime.enable');await send('Network.enable');await send('Network.setCacheDisabled',{cacheDisabled:true});
  await send('Emulation.setDeviceMetricsOverride',{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  events.set('Page.javascriptDialogOpening',[()=>send('Page.handleJavaScriptDialog',{accept:true})]);
  const matrix=[['admin',0,true,true,true,'orders','0'],['storage',1,false,false,false,'orders','2'],['dispatch',2,false,true,false,'transport',''],['customer',3,true,false,false,'orders','']];
  for(const [name,role,create,transport,users,tab,filter] of matrix){
    await login(name);
    const actual=await evaluate(`(()=>{const visible=id=>!document.getElementById(id).classList.contains('hidden');return {role:currentUser.role_id,create:visible('newOrderButton'),transport:visible('tabTransport'),users:visible('tabUsers'),tab:activeTab,filter:document.getElementById('statusFilter').value,actions:[...document.querySelectorAll('#panelOrders [data-action],#panelTransport [data-action]')].map(e=>e.dataset.action)};})()`);
    assert.deepEqual([actual.role,actual.create,actual.transport,actual.users,actual.tab,actual.filter],[role,create,transport,users,tab,filter]);
    const allowed=role===0?['approve','reject','modify','out-stock','start-transport','deliver','complete']:role===1?['out-stock']:role===2?['start-transport','deliver']:['modify','complete'];
    assert(actual.actions.every(action=>allowed.includes(action)));
    const expectedMenus={admin:['menuReview','menuUsers','menuQuery','menuTrack','menuCreate','menuModify','menuStock'],storage:['menuStock','menuQuery','menuTrack'],dispatch:['menuDispatch','menuDeliver','menuDelivered','menuQuery','menuTrack'],customer:['menuCreate','menuModify','menuComplete','menuQuery','menuTrack']};
    assert.deepEqual(await evaluate("[...document.querySelectorAll('.module-menu button')].map(el=>el.id)"),expectedMenus[name]);
    assert.equal(await evaluate("getComputedStyle(document.querySelector('.stats')).display==='none'"),role!==0);
    assert.equal(await evaluate("document.getElementById('mainPage').dataset.role"),String(role));
    // A hidden control is not authorization: every disallowed mutation must be rejected by the server.
    const mutations=[['POST','/api/orders', 'create'],['PUT','/api/orders/not-an-order','modify'],['POST','/api/orders/not-an-order/approve','approve'],['POST','/api/orders/not-an-order/reject','reject'],['POST','/api/orders/not-an-order/out-stock','out-stock'],['POST','/api/orders/not-an-order/start-transport','start-transport'],['POST','/api/orders/not-an-order/deliver','deliver'],['POST','/api/orders/not-an-order/complete','complete'],['POST','/api/users','users']];
    for(const [method,url,action] of mutations){
      if(role===0 || allowed.includes(action) || role===3&&action==='create')continue;
      assert.equal(await evaluate(`fetch(${JSON.stringify(url)},{method:${JSON.stringify(method)},headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:'{}'}).then(r=>r.status)`),403,name+' must not '+action);
    }
    if(role===0) assert.equal(await evaluate("document.querySelector('#orderTable [data-action=approve]')?.textContent"),'审核通过');
    const previousOrigin=await evaluate('performance.timeOrigin');
    await send('Page.reload',{ignoreCache:true});await until(`performance.timeOrigin!==${previousOrigin} && typeof currentUser!=='undefined' && currentUser?.role_id===${role} && !document.getElementById('mainPage').classList.contains('hidden')`,'Identity not restored');
    await until("document.getElementById('statTotal').textContent!=='-'",'Statistics not loaded');
    await evaluate('window.scrollTo(0,0)');
    assert(actual.actions.length>0,'Role matrix must include real actionable fixture orders');
    const capture=await send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(artifacts,`role-${name}.png`),Buffer.from(capture.data,'base64'));
    checks++;console.log('PASS: distinct role homepage, full forbidden-action matrix and reload identity:',name);
  }
  await login('admin');
  for(const width of [900,390]){
    await send('Emulation.setDeviceMetricsOverride',{width,height:900,deviceScaleFactor:1,mobile:false});
    await evaluate('window.scrollTo(0,0)');
    assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),true,'Page overflow at '+width);
    await mouseClick('#menuReview');
    await until("!document.getElementById('tableWrap').classList.contains('hidden')",'Review missing at '+width);
    const shot=await send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(artifacts,`layout-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await send('Emulation.setDeviceMetricsOverride',{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  for(const state of ['', '0','2','3','4','5','6','1']){
    await evaluate("document.getElementById('searchInput').value='old-search'");
    await mouseClick(`.stat-card[data-status="${state}"]`);
    await until(`document.querySelector('.stat-card.active')?.dataset.status===${JSON.stringify(state)} && !document.getElementById('tableWrap').classList.contains('hidden') && document.querySelectorAll('#orderTable tr').length>0`,'Status card did not open records: '+state);
    assert.equal(await evaluate("document.getElementById('searchInput').value"),'');
    assert.equal(await evaluate(`Array.from(document.querySelectorAll('#orderTable .status-pill')).every(el=>${JSON.stringify(state)}===''||el.textContent===statusNames[${JSON.stringify(state)}])`),true);
  }
  await mouseClick('#tabUsers');await mouseClick('.stat-card[data-status="0"]');
  await until("activeTab==='orders' && !document.getElementById('tableWrap').classList.contains('hidden')",'Card did not leave users');
  for(const [selector,module] of [['#menuQuery','query'],['#menuModify','modify'],['#menuTrack','track'],['#menuReview','review']]){
    await mouseClick(selector);await until(`orderModule===${JSON.stringify(module)} && !document.getElementById('tableWrap').classList.contains('hidden')`,'Module click failed');
  }
  const tracked=await evaluate("document.querySelector('#orderTable tr').dataset.orderId");
  await mouseClick('#menuTrack');await evaluate(`document.getElementById('trackOrderId').value=${JSON.stringify(tracked)}`);await mouseClick('#trackLookup button');
  await until("document.getElementById('drawer').classList.contains('open')",'Tracking lookup failed');
  await sleep(300);await mouseClick('.drawer-close');await sleep(300);
  await evaluate("document.getElementById('trackOrderId').value='not-an-order'");await mouseClick('#trackLookup button');
  await until("document.getElementById('trackError').textContent.includes('未找到')",'Tracking lookup failure hidden');
  await mouseClick('#menuReview');await until("!document.getElementById('tableWrap').classList.contains('hidden')",'Review list not loaded');
  await mouseClick('#orderTable tr');await until("document.getElementById('drawer').classList.contains('open')",'Row did not open detail');
  await sleep(300);await mouseClick('#drawerActions [data-action="approve"]');
  await until("document.getElementById('confirmModal').classList.contains('show')",'Drawer approval confirmation absent');
  await mouseClick('#confirmModal .cancel');await mouseClick('.drawer-close');await sleep(300);
  // Reverse two responses: the last clicked status must win.
  assert.equal(await evaluate(`(async()=>{const original=api;let release;try{api=(method,url,...rest)=>url.includes('/api/orders?')?new Promise(resolve=>{if(url.includes('status=0'))release=()=>resolve({items:[],total_count:0});else resolve({items:[],total_count:0});}):original(method,url,...rest);const old=filterStatus('0');await filterStatus('2');release();await old;return document.getElementById('emptyState').textContent.includes('待出库');}finally{api=original;}})()`),true);
  await evaluate("window.savedApi=api;api=async()=>{throw Error('test offline')};filterStatus('0')");
  await until("!document.getElementById('orderError').classList.contains('hidden')",'Load failure hidden');
  await evaluate("api=window.savedApi;delete window.savedApi");await mouseClick('#orderError button');
  await until("!document.getElementById('tableWrap').classList.contains('hidden')",'Retry failed');
  await until("!document.getElementById('toast').classList.contains('show')",'Transient error toast did not clear');
  const updated=await send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(artifacts,'navigation-admin.png'),Buffer.from(updated.data,'base64'));
  checks++;console.log('PASS: physical mouse clicks: all eight statuses, modules, drawer confirmation, response race and retry');
  await login('customer');
  await mouseClick('#menuCreate');
  await evaluate("document.getElementById('oc_name').value='Browser cycle';document.getElementById('oc_cargo').value='Browser box'");await mouseClick('#createBtn');
  await until("!document.getElementById('createModal').classList.contains('show')",'Create did not complete');
  const id=await evaluate("api('GET','/api/orders?keyword=Browser%20cycle').then(d=>d.items[0].order_id)");assert(id);
  await mouseClick('#menuModify');await until(`document.querySelector('tr[data-order-id="${id}"] [data-action="modify"]')!==null && !document.getElementById('tableWrap').classList.contains('hidden')`,'Edit row missing');
  await mouseClick(`tr[data-order-id="${id}"] [data-action="modify"]`);await until("document.getElementById('createModal').classList.contains('show')",'Edit form missing');
  await evaluate(`api('GET','/api/orders/${id}').then(d=>api('PUT','/api/orders/${id}',{cargo_quantity:4,edit_token:d.order.edit_token}))`);
  await evaluate("document.getElementById('oc_qty').value='9'");await mouseClick('#createBtn');
  await until("document.getElementById('createError').textContent.includes('其他人')",'Conflict explanation missing');
  assert.equal(await evaluate("document.getElementById('oc_qty').value"),'9');
  assert.equal(await evaluate(`api('GET','/api/orders/${id}').then(d=>d.order.cargo_quantity)`),4);
  await mouseClick('#createModal .cancel');await evaluate(`editOrder('${id}')`);
  checks++;console.log('PASS: stale edit reports conflict, keeps input and preserves the other writer');
  await evaluate("document.getElementById('oc_qty').value='3';document.getElementById('oc_from').value='UI sender'");await mouseClick('#createBtn');
  await until("!document.getElementById('createModal').classList.contains('show')",'Edit did not complete');
  assert.equal(await evaluate(`api('GET','/api/orders/${id}').then(d=>d.order.cargo_quantity)`),3);
  assert.equal(await evaluate(`fetch('/api/orders/${id}/approve',{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:'{}'}).then(r=>r.status)`),403);
  checks++;console.log('PASS: customer creates/edits; direct unauthorized approval is rejected');
  await login('admin');await mouseClick('#menuReview');await clickAction(id,'approve');await status(id,2);
  await login('storage');await mouseClick('#menuStock');await clickAction(id,'out-stock');await status(id,3);
  await login('dispatch');await clickAction(id,'start-transport');await status(id,4);
  await mouseClick('#menuDeliver');await clickAction(id,'deliver');await status(id,5);
  await login('customer');await mouseClick('#menuComplete');await clickAction(id,'complete');await status(id,6);
  checks++;console.log('PASS: complete order lifecycle via actual action and confirmation buttons');
  await login('admin');await mouseClick('#menuUsers');
  await evaluate("document.getElementById('newUsername').value='browser_new_user';document.getElementById('newUserPassword').value='password123';document.getElementById('newUserRole').value='3';createUser()");
  const userId=await evaluate("api('GET','/api/users?keyword=browser_new_user').then(d=>d.items[0].id)");assert(userId);
  await evaluate(`resetUser('browser_new_user',${userId})`);
  assert.equal(await evaluate("fetch('/api/auth/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:'browser_new_user',password:'888888'})}).then(r=>r.status)"),200);
  checks++;console.log('PASS: admin user list, create user and reset password');
  // Old 401 responses must never clear a newer successful login.
  assert.equal(await evaluate(`(async()=>{const realFetch=fetch,saved=token;let finish;try{token='old-session';window.fetch=()=>new Promise(resolve=>finish=()=>resolve(new Response('{}',{status:401})));const request=api('GET','/api/orders').catch(()=>{});token=saved;finish();await request;return token===saved;}finally{window.fetch=realFetch;token=saved;}})()`),true);
  checks++;console.log('PASS: a stale 401 does not clear a newer session');
  const backend=await evaluate("api('GET','/api/status').then(d=>d.backend)");
  const result=JSON.stringify({checks,passed:true,origin,backend,verifiedAt:new Date().toISOString(),input:'Edge CDP mouse coordinates'},null,2);
  fs.writeFileSync(path.join(artifacts,'browser-results.json'),result);
  fs.writeFileSync(path.join(artifacts,backend.includes('MySQL')?'navigation-mysql-results.json':'navigation-file-results.json'),result);
  console.log(`PASS: ${checks} real-browser checks`);
}finally{
  if(socket?.readyState===WebSocket.OPEN){try{await send('Browser.close');}catch{}socket.close();}
  for(const request of waiting.values()){clearTimeout(request.timer);request.reject(new Error('Browser closed'));}
  await Promise.race([new Promise(resolve=>browser.once('exit',resolve)),sleep(3000)]);if(browser.exitCode===null)browser.kill();fs.closeSync(log);
}
