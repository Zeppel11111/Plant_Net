/* 监控首页交互：轮询数据接口并绘制曲线。 */
/* 曲线数据：[秒级时间戳, 温度x10, 湿度x10, 光照]，按时间升序（旧的在前） */
var DATA = window.PLANT_NET_DATA.points;
/* 土壤曲线数据：[秒级时间戳, 温度x10, 湿度x10, EC, 盐度, N, P, K, pHx10] */
var SOIL_DATA = window.PLANT_NET_DATA.soil_points;
var SOIL2_DATA = window.PLANT_NET_DATA.soil2_points;
var ACTIVE_GROUP = "air";
var ACTIVE_SOIL = 1;
var MAX_SAMPLE_GAP_SECONDS = 10 * 60;

/* ---------- 工具 ---------- */
function fmtHM(ep){var d=new Date(ep*1000);function p(n){return (n<10?"0":"")+n}
  return p(d.getHours())+":"+p(d.getMinutes())+":"+p(d.getSeconds());}
function fmtDateTime(ep){var d=new Date(ep*1000);function p(n){return (n<10?"0":"")+n}
  return d.getFullYear()+"-"+p(d.getMonth()+1)+"-"+p(d.getDate())+" "+fmtHM(ep);}
function lastValidSample(rows,column){
  for(var i=rows.length-1;i>=0;i--){
    var value=rows[i][column];
    if(value!==null&&value!==undefined)return [rows[i][0],value];
  }
  return null;
}

/* 读数卡片与状态：用最新一点更新 */
var API=(location.protocol==="http:"||location.protocol==="https:")?"/api/latest":null;

function renderHead(){
  var dot=document.getElementById("dot"), lt=document.getElementById("liveText"),
      st=document.getElementById("stime");
  if(DATA.length===0){
    dot.className="dot off";
    lt.textContent="等待数据…";
    st.textContent="--:--:--";
    ["Temp","Humi","Lux"].forEach(function(name){
      document.getElementById("v"+name).textContent="暂无数据";
      document.getElementById("n"+name).textContent="等待首次采集…";
    });
    return;
  }
  var last=DATA[DATA.length-1];
  st.textContent=fmtHM(last[0]);
  [["Temp",1,true],["Humi",2,true],["Lux",3,false]].forEach(function(item){
    var sample=lastValidSample(DATA,item[1]);
    document.getElementById("v"+item[0]).textContent=sample?(item[2]?formatAirX10(sample[1]):sample[1]):"暂无数据";
    document.getElementById("n"+item[0]).textContent=sample?"最近有效采集 "+fmtDateTime(sample[0]):"等待首次采集…";
  });
}

function formatAirX10(value){return (value/10).toFixed(1);}

/* 漂亮的刻度：把 [lo,hi] 分成大约 n 段的整数步长 */
function niceTicks(lo,hi,n){
  if(hi<=lo)return [lo];
  var rough=(hi-lo)/n, mag=Math.pow(10,Math.floor(Math.log(rough)/Math.LN10));
  var norm=rough/mag, step=norm<1.5?1:norm<3?2:norm<7?5:10; step*=mag;
  var out=[]; for(var v=Math.ceil(lo/step)*step; v<=hi+step*0.001; v+=step) out.push(v);
  return out;
}

/* 画一条曲线：values 为数值数组，epochs 为对应时间戳 */
function drawSeries(canvasId, values, epochs, color){
  var cv=document.getElementById(canvasId);
  var cssW=cv.parentElement.clientWidth, cssH=118;
  var dpr=window.devicePixelRatio||1;
  cv.width=cssW*dpr; cv.height=cssH*dpr;
  var ctx=cv.getContext("2d");
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,cssW,cssH);
  var usable=[];
  for(var usableIndex=0;usableIndex<values.length;usableIndex++){
    if(values[usableIndex]!==null)usable.push(values[usableIndex]);
  }
  if(usable.length===0){
    ctx.fillStyle="rgba(21,56,78,.36)";ctx.font="12px 'Cascadia Code',Consolas,monospace";
    ctx.fillText("数据采集中，稍候…",12,cssH/2);
    return;
  }
  /* y 范围：留 12% 上下余量 */
  var lo=Math.min.apply(null,usable), hi=Math.max.apply(null,usable);
  var pad=(hi-lo)*0.15||1; lo-=pad; hi+=pad;
  var padL=46, padR=14, padT=10, padB=18;
  var pw=cssW-padL-padR, ph=cssH-padT-padB;
  var X=function(i){return values.length===1?padL+pw/2:padL+pw*i/(values.length-1);};
  var Y=function(v){return padT+ph*(1-(v-lo)/(hi-lo));};
  /* 网格与刻度 */
  ctx.strokeStyle="rgba(21,56,78,.08)";ctx.fillStyle="rgba(21,56,78,.38)";
  ctx.font="10px 'Cascadia Code',Consolas,monospace";ctx.lineWidth=1;
  var ticks=niceTicks(lo,hi,4);
  for(var k=0;k<ticks.length;k++){
    var y=Y(ticks[k]);
    ctx.beginPath();ctx.moveTo(padL,y);ctx.lineTo(cssW-padR,y);ctx.stroke();
    ctx.textAlign="right";ctx.fillText(ticks[k].toFixed(ticks[k]%1?1:0),padL-6,y+3);
  }
  /* 横轴时间刻度：5 个 */
  for(var t=0;t<(values.length===1?1:5);t++){
    var i=Math.round(t*(values.length-1)/4);
    var x=X(i);
    ctx.beginPath();ctx.moveTo(x,cssH-padB);ctx.lineTo(x,cssH-padB+4);ctx.stroke();
    ctx.textAlign="center";ctx.fillText(fmtHM(epochs[i]),x,cssH-3);
  }
  /* 曲线：null 中断路径，绝不将缺失读数补成零或跨点连线。 */
  ctx.lineWidth=1.6;ctx.strokeStyle=color;
  ctx.beginPath();
  var drawing=false,lastIndex=-1;
  for(var i2=0;i2<values.length;i2++){
    if(values[i2]===null){drawing=false;continue;}
    var x2=X(i2),y2=Y(values[i2]);
    if(!drawing){ctx.moveTo(x2,y2);drawing=true;}else ctx.lineTo(x2,y2);
    lastIndex=i2;
  }
  ctx.stroke();
  /* 末端圆点 + 数值 */
  var lx=X(lastIndex), ly=Y(values[lastIndex]);
  ctx.fillStyle=color;ctx.beginPath();ctx.arc(lx,ly,3.5,0,6.2832);ctx.fill();
  ctx.fillStyle=color;ctx.font="600 11px 'Cascadia Code',Consolas,monospace";
  ctx.textAlign="left";ctx.fillText(values[lastIndex],lx+8,ly-6);
}

/* 空状态 */
function renderEmpty(){
  document.querySelectorAll("#panel-air .series canvas").forEach(function(cv){
    var ctx=cv.getContext("2d"), cssW=cv.parentElement.clientWidth;
    cv.width=cssW;cv.height=118;
    ctx.fillStyle="rgba(21,56,78,.36)";ctx.font="13px 'Cascadia Code',Consolas,monospace";
    ctx.fillText("还没有数据 —— 板子连接后自动出现",cssW/2-130,59);
  });
}

/* 入口 */
function chartArrays(){
  return {t:chartSeries(1,10),h:chartSeries(2,10),l:chartSeries(3,1)};
}

function chartSeries(column,scale){
  var eps=[],values=[];
  for(var i=0;i<DATA.length;i++){
    var value=DATA[i][column];
    eps.push(DATA[i][0]);values.push(value===null?null:value/scale);
  }
  return {eps:eps,values:values};
}

function activeSoilData(){return ACTIVE_SOIL===2?SOIL2_DATA:SOIL_DATA;}

function soilSeries(column,scale){
  var data=activeSoilData();
  var eps=[],values=[];
  for(var i=0;i<data.length;i++){
    if(i>0&&data[i][0]-data[i-1][0]>MAX_SAMPLE_GAP_SECONDS){
      eps.push(data[i-1][0]+1);values.push(null);
    }
    var value=data[i][column];
    eps.push(data[i][0]);values.push(value===null?null:value/scale);
  }
  return {eps:eps,values:values};
}

function setSoilReading(valueId,noteId,sample,scale){
  var valueNode=document.getElementById(valueId),noteNode=document.getElementById(noteId);
  if(!valueNode||!noteNode)return;
  if(!sample){
    valueNode.textContent="暂无数据";
    noteNode.textContent="等待首次采集…";
    return;
  }
  valueNode.textContent=scale===10?(sample[1]/10).toFixed(1):sample[1];
  noteNode.textContent="最近有效采集 "+fmtDateTime(sample[0]);
}

function renderSoilHead(){
  var data=activeSoilData();
  if(data.length===0){
    ["SoilTemp","SoilHumi","SoilEc","SoilSalinity","SoilN","SoilP","SoilK","SoilPh"].forEach(function(name){
      var value=document.getElementById("v"+name),note=document.getElementById("n"+name);
      if(value)value.textContent="暂无数据";if(note)note.textContent="等待数据…";
    });
    return;
  }
  setSoilReading("vSoilTemp","nSoilTemp",lastValidSample(data,1),10);
  setSoilReading("vSoilHumi","nSoilHumi",lastValidSample(data,2),10);
  setSoilReading("vSoilEc","nSoilEc",lastValidSample(data,3),1);
  setSoilReading("vSoilSalinity","nSoilSalinity",lastValidSample(data,4),1);
  setSoilReading("vSoilN","nSoilN",lastValidSample(data,5),1);
  setSoilReading("vSoilP","nSoilP",lastValidSample(data,6),1);
  setSoilReading("vSoilK","nSoilK",lastValidSample(data,7),1);
  setSoilReading("vSoilPh","nSoilPh",lastValidSample(data,8),10);
}

/* 将同单位的 N/P/K 画在一张图，null 会形成可见断点而不是被当作零。 */
function drawMultiSeries(canvasId,series){
  var data=activeSoilData();
  var cv=document.getElementById(canvasId);if(!cv)return;
  var cssW=cv.parentElement.clientWidth,cssH=118,dpr=window.devicePixelRatio||1;
  cv.width=cssW*dpr;cv.height=cssH*dpr;
  var ctx=cv.getContext("2d");ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,cssW,cssH);
  var values=[];
  for(var s=0;s<series.length;s++)for(var i=0;i<data.length;i++){
    var raw=data[i][series[s].column];if(raw!==null)values.push(raw/series[s].scale);
  }
  if(values.length<2){ctx.fillStyle="rgba(21,56,78,.36)";ctx.font="12px 'Cascadia Code',Consolas,monospace";ctx.fillText("数据采集中，稍候…",12,cssH/2);return;}
  var lo=Math.min.apply(null,values),hi=Math.max.apply(null,values),pad=(hi-lo)*.15||1;lo-=pad;hi+=pad;
  var padL=46,padR=14,padT=10,padB=18,pw=cssW-padL-padR,ph=cssH-padT-padB;
  var X=function(i){return padL+pw*i/(data.length-1||1);};
  var Y=function(v){return padT+ph*(1-(v-lo)/(hi-lo));};
  ctx.strokeStyle="rgba(21,56,78,.08)";ctx.fillStyle="rgba(21,56,78,.38)";ctx.font="10px 'Cascadia Code',Consolas,monospace";
  var ticks=niceTicks(lo,hi,4);for(var t=0;t<ticks.length;t++){var y=Y(ticks[t]);ctx.beginPath();ctx.moveTo(padL,y);ctx.lineTo(cssW-padR,y);ctx.stroke();ctx.textAlign="right";ctx.fillText(ticks[t].toFixed(ticks[t]%1?1:0),padL-6,y+3);}
  for(var s2=0;s2<series.length;s2++){
    ctx.strokeStyle=series[s2].color;ctx.lineWidth=1.6;ctx.beginPath();var drawing=false;
    for(var j=0;j<data.length;j++){var point=data[j][series[s2].column];if(point===null){drawing=false;continue;}if(j>0&&data[j][0]-data[j-1][0]>MAX_SAMPLE_GAP_SECONDS)drawing=false;var px=X(j),py=Y(point/series[s2].scale);if(!drawing){ctx.moveTo(px,py);drawing=true;}else ctx.lineTo(px,py);}
    ctx.stroke();
  }
}

/* 两只探头在同一张图上按数据库记录时间对齐，使用相同的纵轴。 */
function drawSoilComparison(canvasId,column,scale){
  var cv=document.getElementById(canvasId);if(!cv)return;
  var width=cv.parentElement.clientWidth,height=150,dpr=window.devicePixelRatio||1;
  if(width<=0)return;
  cv.width=width*dpr;cv.height=height*dpr;
  var ctx=cv.getContext("2d");ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,width,height);
  var datasets=[{rows:SOIL_DATA,color:"#17b184"},{rows:SOIL2_DATA,color:"#e0a63a"}];
  var values=[],times=[];
  datasets.forEach(function(set){set.rows.forEach(function(row){
    times.push(row[0]);
    if(row[column]!==null&&row[column]!==undefined)values.push(row[column]/scale);
  });});
  if(!values.length){
    ctx.fillStyle="rgba(21,56,78,.42)";ctx.font="12px 'Cascadia Code',Consolas,monospace";
    ctx.fillText("暂无有效读数",12,height/2);return;
  }
  var minTime=Math.min.apply(null,times),maxTime=Math.max.apply(null,times);
  if(maxTime===minTime)maxTime=minTime+1;
  var low=Math.min.apply(null,values),high=Math.max.apply(null,values);
  var padding=(high-low)*.15||1;low-=padding;high+=padding;
  var left=48,right=16,top=12,bottom=22,plotW=Math.max(1,width-left-right),plotH=height-top-bottom;
  var X=function(epoch){return left+plotW*(epoch-minTime)/(maxTime-minTime);};
  var Y=function(value){return top+plotH*(1-(value-low)/(high-low));};
  ctx.lineWidth=1;ctx.strokeStyle="rgba(21,56,78,.09)";
  ctx.fillStyle="rgba(21,56,78,.46)";ctx.font="10px 'Cascadia Code',Consolas,monospace";
  niceTicks(low,high,3).forEach(function(tick){
    var y=Y(tick);ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(width-right,y);ctx.stroke();
    ctx.textAlign="right";ctx.fillText(tick.toFixed(tick%1?1:0),left-7,y+3);
  });
  for(var i=0;i<3;i++){
    var epoch=minTime+(maxTime-minTime)*i/2,x=X(epoch);
    ctx.textAlign=i===0?"left":i===2?"right":"center";
    ctx.fillText(fmtHM(epoch),x,height-3);
  }
  datasets.forEach(function(set){
    ctx.strokeStyle=set.color;ctx.lineWidth=2;ctx.beginPath();
    var previous=null,last=null;
    set.rows.forEach(function(row){
      var raw=row[column];
      if(raw===null||raw===undefined){previous=null;return;}
      var x=X(row[0]),y=Y(raw/scale);
      if(previous===null||row[0]-previous>MAX_SAMPLE_GAP_SECONDS)ctx.moveTo(x,y);else ctx.lineTo(x,y);
      previous=row[0];last={x:x,y:y};
    });
    ctx.stroke();
    if(last){ctx.fillStyle=set.color;ctx.beginPath();ctx.arc(last.x,last.y,3.5,0,Math.PI*2);ctx.fill();}
  });
}

function drawAll(){
  renderHead();
  if(ACTIVE_GROUP==="air"){
    if(DATA.length===0){renderEmpty();return;}
    var a=chartArrays();
    drawSeries("chTemp",a.t.values,a.t.eps,"#ef7c5f");
    drawSeries("chHumi",a.h.values,a.h.eps,"#17b184");
    drawSeries("chLux",a.l.values,a.l.eps,"#e0a63a");
    return;
  }
  [["chSoilTemp",1,10],["chSoilHumi",2,10],["chSoilEc",3,1],
   ["chSoilSalinity",4,1],["chSoilN",5,1],["chSoilP",6,1],
   ["chSoilK",7,1],["chSoilPh",8,10]].forEach(function(item){
    drawSoilComparison(item[0],item[1],item[2]);
  });
}

function esc(s){
  return String(s).replace(/[&<>"']/g,function(c){
    return {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c];});
}

function deviceLabel(device,validMask){
  if(device==="tas01")return "塔石传感器 (tas01 · 0x01)";
  if(device==="soil02")return "土壤传感1 (soil02 · 0x02)";
  if(device==="soil03")return "土壤传感2 (soil03 · 0x03)";
  return device;
}

function renderGroupRows(id,rows,fields){
  var tb=document.getElementById(id);if(!tb)return;
  if(!rows.length){
    tb.innerHTML='<tr class="empty-row"><td colspan="'+fields.length+'">等待该设备的有效读数…</td></tr>';
    return;
  }
  var html="";
  for(var i=0;i<rows.length;i++){
    html+="<tr>";
    for(var j=0;j<fields.length;j++){
      html+="<td>"+esc(displaySoil(rows[i][fields[j]]))+"</td>";
    }
    html+="</tr>";
  }
  tb.innerHTML=html;
}

function renderRows(groups){
  if(!groups)return;
  renderGroupRows("airRows",groups.air||[],["time","temp","humi","lux"]);
  var soilFields=["time","soil_temp","soil_humi","soil_ec","soil_salinity",
                  "soil_n","soil_p","soil_k","soil_ph"];
  renderGroupRows("soil1Rows",groups.soil1||[],soilFields);
  renderGroupRows("soil2Rows",groups.soil2||[],soilFields);
}

function displaySoil(value){return value===null||value===undefined?"暂无数据":value;}

function activatePanel(name,focusTab){
  ACTIVE_GROUP=name;
  document.querySelectorAll(".tab").forEach(function(tab){
    var active=tab.dataset.panel===name;
    tab.setAttribute("aria-selected",active?"true":"false");
    tab.setAttribute("tabindex",active?"0":"-1");
    if(active&&focusTab)tab.focus();
  });
  document.querySelectorAll(".data-panel").forEach(function(panel){panel.hidden=panel.id!=="panel-"+name;});
  drawAll();
}

document.querySelectorAll(".tab").forEach(function(tab){
  tab.addEventListener("click",function(){activatePanel(tab.dataset.panel);});
  tab.addEventListener("keydown",function(event){
    var tabs=Array.prototype.slice.call(document.querySelectorAll(".tab")),index=tabs.indexOf(tab),next=index;
    if(event.key==="ArrowLeft")next=(index-1+tabs.length)%tabs.length;
    else if(event.key==="ArrowRight")next=(index+1)%tabs.length;
    else if(event.key==="Home")next=0;
    else if(event.key==="End")next=tabs.length-1;
    else return;
    event.preventDefault();activatePanel(tabs[next].dataset.panel,true);
  });
});

function setLive(ok){
  var dot=document.getElementById("dot"),lt=document.getElementById("liveText");
  var want=ok?"dot":"dot off",text=ok?"数据流监测中":"连接中断，重试中…";
  if(dot&&dot.className!==want)dot.className=want;
  if(lt&&lt.textContent!==text)lt.textContent=text;
}

function poll(){
  if(!API)return;
  fetch(API,{cache:"no-store"})
    .then(function(res){if(!res.ok)throw new Error("HTTP "+res.status);return res.json();})
    .then(function(d){
      if(d&&Array.isArray(d.points)){DATA=d.points;}
      if(d&&Array.isArray(d.soil_points)){SOIL_DATA=d.soil_points;}
      if(d&&Array.isArray(d.soil2_points)){SOIL2_DATA=d.soil2_points;}
      drawAll();
      if(d&&d.table_rows)renderRows(d.table_rows);
      setLive(true);
    })
    .catch(function(){setLive(false);});
}

/* 启动：先画当前数据，再每 5 秒静默更新 */
drawAll();
renderRows(window.PLANT_NET_DATA.table_rows);
setInterval(poll,5000);

/* ---------- 拍照存库：服务器拉 CAM /snap → 落盘入库 → 页面上显示 ---------- */
function snapNow(){
  var b=document.getElementById("snapBtn"), box=document.getElementById("snapBox");
  if(!box)return;
  box.style.display="block";
  if(b){b.disabled=true;b.textContent="拍照中…";}
  box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:var(--ink-faint)">正在请 CAM 拍照…</span>';
  fetch("/api/snap",{cache:"no-store"})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.ok){
        box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:var(--ink)">已存库: '
          +d.path+'</span><br>'
          +'<a href="/'+d.path+'" target="_blank"><img src="/'+d.path
          +'" style="max-width:100%;max-height:360px;border-radius:12px;margin-top:6px;box-shadow:0 12px 30px -18px rgba(35,110,170,.5)"></a>';
      }else{
        box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:#c0392b">截图失败: '
          +(d.err||"未知")+'</span>';
      }
    })
    .catch(function(){box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:#c0392b">截图失败: 网络错误</span>';})
    .then(function(){if(b){b.disabled=false;b.textContent="拍照存库";}});
}

/* ---------- 摄像头调试：由 Flask 转发设置到当前连接的 CAM ---------- */
(function initCameraSettings(){
  var details=document.getElementById("cameraDebug"),form=document.getElementById("cameraSettingsForm");
  if(!details||!form)return;
  var fps=document.getElementById("cameraFps"),quality=document.getElementById("cameraQuality"),
      fpsValue=document.getElementById("cameraFpsValue"),qualityValue=document.getElementById("cameraQualityValue"),
      actual=document.getElementById("cameraActualFps"),status=document.getElementById("cameraSettingsStatus"),
      button=document.getElementById("cameraApply"),loading=false;
  function showValues(){fpsValue.textContent=fps.value+" FPS";qualityValue.textContent=quality.value;}
  fps.addEventListener("input",showValues);
  quality.addEventListener("input",showValues);

  async function readSettings(fillInputs){
    if(loading)return;
    loading=true;
    try{
      var response=await fetch("/api/camera/settings",{cache:"no-store"});
      var data=await response.json();
      if(!response.ok||!data.ok)throw new Error(data.err||"读取失败");
      if(fillInputs){fps.value=data.fps_limit;quality.value=data.quality;showValues();}
      actual.textContent="实际推流 FPS："+Number(data.actual_fps).toFixed(1);
      status.textContent="当前 CAM 设置已读取";
    }catch(error){
      actual.textContent="实际推流 FPS：--";
      status.textContent=error.message;
    }finally{loading=false;}
  }

  details.addEventListener("toggle",function(){if(details.open)readSettings(true);});
  setInterval(function(){if(details.open)readSettings(false);},10000);
  form.addEventListener("submit",async function(event){
    event.preventDefault();
    if(loading)return;
    loading=true;button.disabled=true;status.textContent="正在应用到 CAM…";
    try{
      var response=await fetch("/api/camera/settings",{
        method:"POST",headers:{"Content-Type":"application/json"},
        body:JSON.stringify({fps_limit:Number(fps.value),quality:Number(quality.value)})
      });
      var data=await response.json();
      if(!response.ok||!data.ok)throw new Error(data.err||"设置失败");
      fps.value=data.fps_limit;quality.value=data.quality;showValues();
      actual.textContent="实际推流 FPS："+Number(data.actual_fps).toFixed(1);
      status.textContent="已应用；约 10 秒后更新实际 FPS";
    }catch(error){status.textContent=error.message;}
    finally{button.disabled=false;loading=false;}
  });
})();

/* ---------- 监控：Flask MJPEG 实时流（/api/live，浏览器一次连接持续收帧） ---------- */
function refreshCam(){
  var box=document.getElementById("monBox"), img=document.getElementById("liveCam"),
      hint=document.getElementById("monHint");
  if(!box||!img)return;
  img.src="/api/live?v="+Date.now();     /* 首次挂流；带时间戳便于断流后重连 */
  box.style.display="block";
  if(hint)hint.textContent="实时画面（Flask 收 CAM 推流转播）· 每 60s 自动截取存库";
}
/* 每 20s 重挂一次流：浏览器断流自愈 */
setInterval(function(){
  var img=document.getElementById("liveCam");
  if(img) img.src="/api/live?v="+Date.now();
},20000);
refreshCam();
