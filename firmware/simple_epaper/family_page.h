#pragma once

// Kept in the application image so the phone can load it without Internet or SD.
constexpr char FamilyPage[] = R"familyweb(<!doctype html>
<html lang="zh-CN">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>小艺墨水屏</title>
<style>
  :root{color-scheme:light;font:16px/1.55 system-ui,sans-serif;color:#23332b;background:#f4f2ea;--ink:#204d3b;--line:#c7ccbf;--paper:#fffef8}
  *{box-sizing:border-box}body{margin:0}main{max-width:780px;margin:auto;padding:0 clamp(14px,4vw,32px) 48px}
  header{padding:24px 0 8px}h1{font:600 clamp(1.85rem,5vw,2.55rem)/1.24 system-ui,sans-serif;letter-spacing:-.045em;margin:0}
  h2,h3{margin:0;font-size:1.2rem}p{margin:8px 0}small,.hint{color:#49584d;font-size:.9rem}
  nav{position:sticky;top:0;z-index:2;display:flex;gap:4px;padding:5px;background:#f4f2ea;border-bottom:1px solid var(--line);margin-bottom:14px}
  nav a{flex:1;text-align:center;color:var(--ink);padding:9px 4px;min-height:44px;border-radius:8px;text-decoration:none;font-weight:650}nav a:hover,nav a:focus-visible{background:#e0e9dd}
  section{scroll-margin-top:68px;border:1px solid var(--line);background:var(--paper);border-radius:16px;padding:clamp(14px,3vw,24px);margin:12px 0}
  .row,.heading,.toolbar{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.row,.toolbar{margin-top:10px}.heading{justify-content:space-between}
  button,select,.picker,summary,input{font:inherit}button,select,.picker,summary{min-height:44px}
  button,select,.picker{padding:8px 13px;color:#253d30;background:#fffef8;border:1px solid #89998b;border-radius:9px;max-width:100%}
  button,.picker,summary{cursor:pointer}.primary{background:var(--ink);color:white;border-color:var(--ink);font-weight:650}
  button:disabled,select:disabled{opacity:.48;cursor:default}.danger{color:#89392c;border-color:#ba8b80}
  :focus-visible{outline:3px solid #367c86;outline-offset:2px}[hidden]{display:none!important}
  .picker{position:relative;display:inline-flex;align-items:center;justify-content:center;overflow:hidden}.picker input{position:absolute;inset:0;width:100%;height:100%;opacity:0;cursor:pointer}.picker:focus-within{outline:3px solid #367c86}
  .row label{max-width:100%}select{background-color:#fff}.row select{min-width:0}
  canvas{display:block;width:100%;aspect-ratio:5/3;background:#e7e5d9;border-radius:8px}
  #displayThumb{margin:12px 0 0}#preview{margin:10px 0}#preview[data-ready]{touch-action:none;cursor:grab}#preview[data-ready]:active{cursor:grabbing}
  .crop-tools{display:flex;align-items:center;gap:9px;flex-wrap:wrap;margin:10px 0}.crop-tools input{flex:1;min-width:68px;height:44px;accent-color:var(--ink)}
  details{border-top:1px solid var(--line);margin-top:12px;padding-top:4px}summary{display:list-item;padding:10px 0;font-weight:600}
  #notice:not(:empty),#layoutNote:not(:empty){padding:10px 13px;background:#edf2e6;border-radius:8px}
  #photoLabel{overflow-wrap:anywhere;font-weight:600}#draftJump{max-width:100%}
  .gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(138px,100%),1fr));gap:10px;margin-top:10px}
  .slide{display:block;padding:6px;min-width:0;border:1px solid var(--line);border-radius:10px;cursor:pointer}.slide:has(:checked){border:2px solid var(--ink);padding:5px;background:#ecf2e6}
  .caption{display:flex;align-items:center;min-height:44px;gap:5px;flex-wrap:wrap}.playing{color:var(--ink);font-size:.82rem;font-weight:bold}
  dialog{color:inherit;background:var(--paper);border:1px solid var(--line);border-radius:15px;padding:16px;max-width:min(860px,95vw);width:100%;max-height:90vh;overflow:auto}
  dialog::backdrop{background:#14231ccd}.orderRow{display:flex;align-items:center;flex-wrap:wrap;gap:6px;border-bottom:1px solid var(--line);padding:7px 0}
  .orderRow canvas{width:66px}.orderRow span{flex:1;min-width:38px}.orderRow button{padding:6px 10px}
  #notice{scroll-margin-top:65px}#screenHint{min-height:1.5em}#status{margin:6px 0;color:var(--ink)}
  @media(max-width:420px){section{border-radius:12px}button,.picker,select{padding:8px 10px}.orderRow canvas{width:48px}}
</style>
<main>
<header><h1>小艺墨水屏</h1><p id="status" role="status">正在连接墨水屏。</p><p id="timing" class="hint" hidden></p><button id="retry" hidden>重新连接</button></header>
<nav aria-label="页面导航"><a href="#screen">当前画面</a><a href="#composer">投照片</a><a href="#gallery">管理轮播</a></nav>
<section id="screen" aria-labelledby="displayTitle">
 <div class="heading"><h2 id="displayTitle">当前画面</h2><button id="enlarge" hidden>放大查看</button></div>
 <canvas id="displayThumb" width="80" height="48" aria-label="墨水屏确认的画面" hidden></canvas>
 <p id="displayNote" class="hint">等待读取屏幕状态。</p>
</section>
<section id="composer" aria-labelledby="composeTitle"><div class="heading"><h2 id="composeTitle">投照片</h2>
 <label class="picker primary" id="pickLabel"><span id="pickText">从相册选照片</span><input id="photos" aria-label="从相册选照片" type="file" accept="image/*" multiple></label></div>
 <p id="pickHint" class="hint">选 1 张投到屏上，选 2 至 12 张可轮播。</p>
 <div id="editor" hidden>
 <div class="row"><button id="prev">上一张</button><button id="next">下一张</button><span id="photoLabel"></span></div>
 <canvas id="preview" width="800" height="480" tabindex="0" aria-label="上屏取景" aria-describedby="cropHelp"></canvas>
 <p id="cropHelp" class="hint">拖动取景，双指或滚轮缩放。也可以用下方按钮微调。</p>
 <div class="row"><label for="framing">取景</label><select id="framing"><option value="fill">铺满屏幕</option><option value="fit">保留整张 · 留白</option></select>
 <button id="proof">看六色效果</button></div>
 <div class="crop-tools" id="zoomTools"><label for="zoom">缩放</label><button id="zoomOut" aria-label="缩小">−</button><input id="zoom" type="range" min="1" max="4" step=".01" value="1"><button id="zoomIn" aria-label="放大">＋</button><output id="scale" for="zoom">1×</output></div>
 <div class="row"><label for="inkMode">六色风格</label><select id="inkMode"><option value="detail">层次增强</option><option value="classic">经典</option></select></div>
 <details id="adjust"><summary>微调照片位置</summary><div class="row" id="pan"><button data-dx="-1" aria-label="向左移动">←</button><button data-dx="1" aria-label="向右移动">→</button><button data-dy="-1" aria-label="向上移动">↑</button><button data-dy="1" aria-label="向下移动">↓</button><button id="resetCrop">复位</button></div></details>
 <details id="draftTools"><summary>整理待投照片</summary><div class="row"><label for="draftJump">跳到照片</label><select id="draftJump"></select>
 <label class="picker">继续添加<input id="append" type="file" aria-label="继续添加照片" accept="image/*" multiple></label></div>
 <div class="row"><button id="earlier">往前移</button><button id="later">往后移</button><button id="removeDraft">移出待投</button></div></details>
 <button id="swapReplacement" hidden>对调两张的替换位置</button>
 <div class="row"><label id="intervalLabel" for="interval">换图间隔</label><select id="interval"><option value="15">15 分钟</option><option value="30" selected>30 分钟</option><option value="60">60 分钟</option></select></div>
 <div class="toolbar"><button id="send" class="primary">投当前图</button><button id="start" class="primary">开始轮播</button><button id="cancelReplace" hidden>取消替换</button></div>
 </div><p id="notice" role="status"></p>
</section>
<section id="gallery" aria-labelledby="albumTitle"><div class="heading"><h2 id="albumTitle">轮播列表</h2><button id="organize" hidden>整理轮播</button></div>
 <p id="albumSummary">正在读取列表。</p><p id="albumHint" class="hint"></p><div id="album" class="gallery"></div>
 <div class="row" id="galleryActions"><button id="resume" hidden>重试轮播</button><button id="replace">替换所选</button><button id="stop" class="danger">清空轮播</button><input id="replacementFiles" type="file" accept="image/*" multiple hidden></div>
 <p id="albumNotice" role="status"></p>
</section>
<dialog id="lightbox" aria-labelledby="largeTitle"><div class="heading"><h2 id="largeTitle">当前画面</h2><button id="closeLarge" autofocus>关闭</button></div><canvas id="largeCanvas" aria-label="放大的已确认画面"></canvas></dialog>
<dialog id="layout" aria-labelledby="layoutTitle"><div class="heading"><h2 id="layoutTitle">整理轮播</h2><button id="cancelLayout" autofocus>取消</button></div>
 <div id="orderList"></div><div class="row"><label for="layoutInterval">换图间隔</label><select id="layoutInterval"><option value="15">15 分钟</option><option value="30">30 分钟</option><option value="60">60 分钟</option></select></div>
 <p id="layoutNote" role="status"></p><button id="saveLayout" class="primary">保存轮播</button></dialog>
</main>
<script>
"use strict";
const WIDTH = 800, HEIGHT = 480, FRAME_BYTES = WIDTH * HEIGHT / 2;
const THUMB_WIDTH = 80, THUMB_HEIGHT = 48, THUMB_BYTES = THUMB_WIDTH * THUMB_HEIGHT / 2;
const GALLERY_WIDTH = 160, GALLERY_HEIGHT = 96, GALLERY_BYTES = GALLERY_WIDTH * GALLERY_HEIGHT * 3;
const palette = [[67,52,57],[200,197,182],[216,177,48],[161,60,34],[63,89,145],[88,93,73]];
const shaConstants = [
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
];
function rotate(value, bits) { return (value >>> bits) | (value << (32 - bits)); }
function sha256(input) {
  // SubtleCrypto is not available in ordinary local HTTP contexts.
  const padded = new Uint8Array(Math.ceil((input.length + 9) / 64) * 64);
  padded.set(input);
  padded[input.length] = 128;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 4, input.length * 8);
  const hash = new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);
  const words = new Uint32Array(64);
  for (let offset = 0; offset < padded.length; offset += 64) {
    for (let i = 0; i < 16; i++) words[i] = view.getUint32(offset + 4*i);
    for (let i = 16; i < 64; i++) {
      const a = words[i-15], b = words[i-2];
      words[i] = words[i-16] + (rotate(a,7)^rotate(a,18)^(a>>>3)) + words[i-7] + (rotate(b,17)^rotate(b,19)^(b>>>10));
    }
    let [a,b,c,d,e,f,g,h] = hash;
    for (let i = 0; i < 64; i++) {
      const t1 = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + shaConstants[i] + words[i];
      const t2 = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
      h=g; g=f; f=e; e=(d+t1)>>>0; d=c; c=b; b=a; a=(t1+t2)>>>0;
    }
    [a,b,c,d,e,f,g,h].forEach((value,i) => { hash[i] += value; });
  }
  return Array.from(hash, word => word.toString(16).padStart(8,"0")).join("");
}
// Match the desktop Pillow converter's six-bit palette lookup and bounded integer diffusion.
function quantizePhoto(source, tone) {
  const frame=new Uint8Array(FRAME_BYTES);
  const row=new Int16Array((WIDTH+1)*3);
  const right=new Int16Array(3), below=new Int16Array(3), previous=new Int16Array(3);
  const rgb=new Uint8Array(3), choices=new Uint8Array(64*64*64);
  for (let y=0;y<HEIGHT;y++) {
    right.fill(0); below.fill(0); previous.fill(0);
    for (let x=0;x<WIDTH;x++) {
      const pixel=y*WIDTH+x, offset=pixel*4, column=x*3;
      const luma=(77*source[offset]+150*source[offset+1]+29*source[offset+2]+128)>>8;
      for (let c=0;c<3;c++) {
        const lifted=Math.min(255,Math.floor(source[offset+c]*tone[luma]));
        const correction=Math.trunc((right[c]+row[column+3+c])/16);
        rgb[c]=Math.min(255,Math.max(0,lifted+correction));
      }
      const r=rgb[0]&252, g=rgb[1]&252, b=rgb[2]&252;
      const key=(r<<10)|(g<<4)|(b>>2);
      let ink=choices[key]-1;
      if (ink<0) {
        let best=Infinity;
        for (let i=0;i<palette.length;i++) {
          const [pr,pg,pb]=palette[i];
          const distance=(r-pr)**2+(g-pg)**2+(b-pb)**2;
          if (distance<best) { best=distance; ink=i; }
        }
        choices[key]=ink+1;
      }
      if (x%2===0) frame[pixel>>1]=ink<<4;
      else frame[pixel>>1]|=ink;
      for (let c=0;c<3;c++) {
        const error=rgb[c]-palette[ink][c];
        row[column+c]=below[c]+3*error;
        below[c]=previous[c]+5*error;
        previous[c]=error;
        right[c]=7*error;
      }
    }
    // Pillow 12.x uses its final blue-channel carries at the right edge.
    row[WIDTH*3]=below[2]; row[WIDTH*3+1]=previous[2]; row[WIDTH*3+2]=previous[2];
  }
  return frame;
}
// Alternate the scan direction. Near gray, slightly favor the less chromatic inks.
function quantizeDetail(source) {
  const frame=new Uint8Array(FRAME_BYTES);
  const pitch=(WIDTH+2)*3;
  let row=new Int16Array(pitch), next=new Int16Array(pitch);
  const carry=new Int16Array(3), rgb=new Uint8Array(3);
  // The palette and three penalty levels stay fixed for the worker's lifetime.
  const choices=quantizeDetail.choices ??= new Uint8Array(3*64*64*64);
  const inkCost=palette.map(color=>Math.max(...color)-Math.min(...color));
  for (let y=0;y<HEIGHT;y++) {
    const step=y%2 ? -1 : 1, end=step>0 ? WIDTH : -1;
    carry.fill(0);
    for (let x=step>0 ? 0 : WIDTH-1;x!==end;x+=step) {
      const pixel=y*WIDTH+x, offset=pixel*4, column=(x+1)*3;
      for (let c=0;c<3;c++) {
        rgb[c]=Math.max(0,Math.min(255,source[offset+c]+Math.trunc((carry[c]+row[column+c])/16)));
      }
      const r=rgb[0]&252, g=rgb[1]&252, b=rgb[2]&252;
      const chroma=Math.max(source[offset],source[offset+1],source[offset+2])-Math.min(source[offset],source[offset+1],source[offset+2]);
      const level=chroma<25 ? 2 : chroma<45 ? 1 : 0;
      const key=(level<<18)|(r<<10)|(g<<4)|(b>>2);
      let ink=choices[key]-1;
      if (ink<0) {
        let best=Infinity;
        for (let i=0;i<palette.length;i++) {
          const [pr,pg,pb]=palette[i];
          const distance=(r-pr)**2+(g-pg)**2+(b-pb)**2+level*12*inkCost[i];
          if (distance<best) { best=distance; ink=i; }
        }
        choices[key]=ink+1;
      }
      frame[pixel>>1]|=x%2 ? ink : ink<<4;
      for (let c=0;c<3;c++) {
        const error=rgb[c]-palette[ink][c];
        carry[c]=7*error;
        next[column-step*3+c]+=3*error;
        next[column+c]+=5*error;
        next[column+step*3+c]+=error;
      }
    }
    const used=row; row=next; next=used; next.fill(0);
  }
  return frame;
}
// Map the ends of the photo's luminance range toward this panel's measured inks.
// Both ends remain solid on a pure black/white input. Protect saturated colors.
function fitInkRange(source, tone) {
  const shadow=new Float32Array(256), highlight=new Float32Array(256);
  for (let luma=0;luma<256;luma++) {
    const t=luma/255;
    shadow[luma]=(1-t)**2.8;
    highlight[luma]=t**2.8;
  }
  const [black,white]=palette;
  for (let i=0;i<source.length;i+=4) {
    const r=source[i],g=source[i+1],b=source[i+2];
    const luma=(77*r+150*g+29*b+128)>>8;
    const chroma=Math.max(r,g,b)-Math.min(r,g,b);
    const weight=1-.85*Math.min(1,chroma/100);
    for (let c=0;c<3;c++) {
      const base=Math.min(255,Math.floor(source[i+c]*tone[luma]));
      const shift=black[c]*shadow[luma]-(255-white[c])*highlight[luma];
      source[i+c]=Math.round(base+weight*shift);
    }
  }
}
function diffuse(source,mode) {
  const histogram=new Uint32Array(256);
  let samples=0;
  for (let y=2;y<HEIGHT;y+=5) for (let x=2;x<WIDTH;x+=5) {
    const i=(y*WIDTH+x)*4;
    histogram[(77*source[i]+150*source[i+1]+29*source[i+2]+128)>>8]++;
    samples++;
  }
  let median=0, seen=histogram[0];
  while (seen*2<samples) seen+=histogram[++median];
  const strength=.045+.15*Math.max(0,112-median)/112;
  const tone=Float32Array.from({length:256},(_,luma)=>{
    const t=luma/255;
    return 1+strength*4*t*(1-t);
  });
  if (mode==="detail") {
    fitInkRange(source,tone);
    return quantizeDetail(source);
  }
  return quantizePhoto(source,tone);
}
const $ = id => document.getElementById(id);
const previewCanvas = $("preview");
const ctx = previewCanvas.getContext("2d");
const scratch = document.createElement("canvas");
scratch.width=WIDTH; scratch.height=HEIGHT;
const paint=scratch.getContext("2d",{willReadFrequently:true});
paint.imageSmoothingQuality="high";
let photos=[], selected=0, activeImage=null, board=null, waiting=null;
let working=false, generation=0, paintQueued=false, proofKey=null;
let layoutDraft=null, pollId=0;
let albumSeen="", albumLoading="", shownSeen="", shownLoading="", replacementPlan=null, replacementDraft=null;
const pointers=new Map();
const markedSlots=new Set();
function setText(element,text) { if (element.textContent!==text) element.textContent=text; }
const setNotice = message => setText($("notice"),message);
const formatTime = ms => {
  const seconds=Math.max(0,Math.ceil(ms/1000));
  return seconds<60 ? seconds+" 秒" : Math.floor(seconds/60)+" 分 "+seconds%60+" 秒";
};
function buttons() {
  const refreshing=board?.state==="refreshing";
  layoutButtons();
  $("editor").hidden=!photos.length;
  const locked=working||!!replacementDraft;
  $("pickLabel").hidden=!!replacementDraft;
  $("photos").disabled=locked;
  setText($("pickText"),photos.length ? "重新选一组" : "从相册选照片");
  $("pickHint").hidden=!!photos.length;
  $("append").disabled=locked||photos.length>=12;
  $("draftTools").hidden=!!replacementDraft;
  $("swapReplacement").hidden=!replacementDraft||photos.length!==2;
  $("swapReplacement").disabled=working||!!waiting;
  $("resume").hidden=!board?.paused; $("resume").disabled=working||!!waiting;
  $("draftJump").disabled=working;
  $("earlier").disabled=locked||selected===0;
  $("later").disabled=locked||selected>=photos.length-1;
  $("removeDraft").disabled=locked||!photos.length;
  $("zoomTools").hidden=$("adjust").hidden=!!photos[selected]?.crop?.fit;
  $("framing").disabled=working||!activeImage;
  $("zoomIn").disabled=working||!activeImage||photos[selected]?.crop?.zoom>=4;
  $("zoomOut").disabled=working||!activeImage||photos[selected]?.crop?.zoom<=1;
  $("pan").querySelectorAll("button").forEach(b=>b.disabled=working||!activeImage);
  $("organize").hidden=$("galleryActions").hidden=!board?.slides;
  $("organize").disabled=working||!!waiting||!!replacementDraft||refreshing||board?.state==="accepted"||albumSeen!==board?.revision;
  setText($("send"),board?.slides ? "结束轮播并投当前图" : "投当前图");
  $("interval").disabled=working;
  $("prev").disabled=working||selected===0;
  $("next").disabled=working||selected>=photos.length-1;
  for (const id of ["zoom","resetCrop","proof","inkMode"]) $(id).disabled=working||!activeImage;
  previewCanvas.toggleAttribute("data-ready",!!activeImage&&!working&&!photos[selected]?.crop?.fit);
  $("send").hidden=!!replacementDraft;
  $("send").disabled=working||!!waiting||refreshing||!activeImage||!board?.photo_ready;
  $("interval").hidden=$("intervalLabel").hidden=!!replacementDraft||photos.length<2;
  $("start").hidden=!replacementDraft&&photos.length<2;
  $("cancelReplace").hidden=!replacementDraft;
  $("cancelReplace").disabled=working;
  setText($("start"),replacementDraft ? "确认替换 "+photos.length+" 张" : board?.slides ? "用所选照片整组换新" : "开始轮播");
  $("start").disabled=working||!!waiting||refreshing||!board?.album_ready||!activeImage||(!replacementDraft&&photos.length<2);
  $("stop").disabled=working||!!waiting||refreshing||board?.state==="accepted"||!!replacementDraft||!board?.slides;
  $("replace").disabled=working||!!waiting||!!replacementDraft||!board?.album_ready||!board.slides||!markedSlots.size;
  const positions=[...markedSlots].sort((a,b)=>a-b).map(i=>i+1).join("、");
  setText($("replace"),positions ? "替换第 "+positions+" 张" : "替换所选");
  $("album").querySelectorAll("input").forEach(box=>{
    box.disabled=working||!!waiting||(!box.checked&&markedSlots.size>=2);
  });
}
async function decoded(file) {
  const url=URL.createObjectURL(file);
  const image=new Image();
  try {
    image.src=url;
    await image.decode();
    return image;
  } finally { URL.revokeObjectURL(url); }
}
const clamp = (value,min,max) => Math.max(min,Math.min(max,value));
function cropSize(image,zoom) {
  const width=Math.min(image.naturalWidth,image.naturalHeight*WIDTH/HEIGHT)/zoom;
  return {width,height:width*HEIGHT/WIDTH};
}
function initialCrop(image) {
  const {height}=cropSize(image,1);
  const spare=image.naturalHeight-height;
  return {x:image.naturalWidth/2,y:height/2+spare*(image.naturalHeight>image.naturalWidth?.20:.50),zoom:1};
}
function drawCrop(context,image,crop) {
  const {width,height}=cropSize(image,crop.zoom);
  context.fillStyle="#fff";
  context.fillRect(0,0,WIDTH,HEIGHT);
  if (crop.fit) {
    const ratio=Math.min(WIDTH/image.naturalWidth,HEIGHT/image.naturalHeight);
    const w=image.naturalWidth*ratio,h=image.naturalHeight*ratio;
    context.drawImage(image,(WIDTH-w)/2,(HEIGHT-h)/2,w,h);
  } else context.drawImage(image,crop.x-width/2,crop.y-height/2,width,height,0,0,WIDTH,HEIGHT);
}
let converter=null, conversionId=0;
const conversions=new Map();
function stopConverter() {
  converter?.terminate(); converter=null;
  for (const job of conversions.values()) job.reject(new Error("图像转换中断"));
  conversions.clear();
}
function runConversion(operation,bytes,mode) {
  if (!converter) {
    const code=`const WIDTH=${WIDTH}, HEIGHT=${HEIGHT}, FRAME_BYTES=${FRAME_BYTES};
const palette=${JSON.stringify(palette)}, shaConstants=${JSON.stringify(shaConstants)};
${rotate}\n${sha256}\n${quantizePhoto}\n${quantizeDetail}\n${fitInkRange}\n${diffuse}
self.onmessage=({data:{id,operation,bytes,mode}})=>{
  try {
    if (operation==="hash") self.postMessage({id,result:sha256(bytes)});
    else { const frame=diffuse(bytes,mode); self.postMessage({id,result:frame},[frame.buffer]); }
  } catch { self.postMessage({id,failed:true}); }
};`;
    const url=URL.createObjectURL(new Blob([code],{type:"text/javascript"}));
    try { converter=new Worker(url); }
    catch (error) { URL.revokeObjectURL(url); throw error; }
    converter.onmessage=({data})=>{
      URL.revokeObjectURL(url);
      const job=conversions.get(data.id);
      if (!job) return;
      conversions.delete(data.id);
      if (data.failed) job.reject(new Error("图像转换失败"));
      else job.resolve(data.result);
    };
    converter.onerror=()=>{ URL.revokeObjectURL(url); stopConverter(); };
  }
  return new Promise((resolve,reject)=>{
    const id=++conversionId;
    conversions.set(id,{resolve,reject});
    if (operation==="hash") converter.postMessage({id,operation,bytes});
    else converter.postMessage({id,operation,bytes,mode},[bytes.buffer]);
  });
}
// Construct native-endian RGBA words from bytes so each pixel needs one store.
const previewInks=new Uint32Array(new Uint8Array(palette.flatMap(([r,g,b])=>[r,g,b,255])).buffer);
function drawPackedPreview(canvas, frame, width, height) {
  if (canvas.width!==width) canvas.width=width;
  if (canvas.height!==height) canvas.height=height;
  const context=canvas.getContext("2d");
  const image=context.createImageData(width,height);
  const pixels=new Uint32Array(image.data.buffer,image.data.byteOffset,width*height);
  for (let i=0;i<frame.length;i++) {
    const pair=frame[i];
    pixels[i*2]=previewInks[pair>>4]; pixels[i*2+1]=previewInks[pair&15];
  }
  context.putImageData(image,0,0);
}
function drawGallery(canvas, rgb) {
  canvas.width=GALLERY_WIDTH; canvas.height=GALLERY_HEIGHT;
  const context=canvas.getContext("2d");
  const image=context.createImageData(GALLERY_WIDTH,GALLERY_HEIGHT);
  for (let i=0;i<GALLERY_WIDTH*GALLERY_HEIGHT;i++) {
    const offset=i*4, source=i*3;
    image.data[offset]=rgb[source]; image.data[offset+1]=rgb[source+1]; image.data[offset+2]=rgb[source+2]; image.data[offset+3]=255;
  }
  context.putImageData(image,0,0);
}
async function thumbnailBytes(path,size,packed=false) {
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),5000);
  try {
    const response=await fetch(path,{mode:"same-origin",cache:"no-store",signal:controller.signal});
    if (!response.ok) throw new Error("缩略图不可用");
    const bytes=new Uint8Array(await response.arrayBuffer());
    if (bytes.length!==size || (packed&&bytes.some(value=>(value>>4)>5||(value&15)>5))) throw new Error("缩略图数据无效");
    return bytes;
  } finally { clearTimeout(timer); }
}
function displayBadges() {
  $("album").querySelectorAll(".playing").forEach((badge, index)=>{
    setText(badge,board?.shown_slot===index ? "正在显示" : "");
  });
}
async function loadAlbum(data) {
  const revision=data.revision;
  if (albumSeen===revision || albumLoading===revision) return;
  if (!data.slides) {
    $("album").replaceChildren(); markedSlots.clear(); albumSeen=revision;
    setText($("albumHint"),"从上方选两张或更多照片即可开始轮播。"); return;
  }
  albumLoading=revision;
  $("album").replaceChildren(); markedSlots.clear(); buttons();
  try {
    const bytes=await thumbnailBytes("/web/album-previews?revision="+encodeURIComponent(revision),data.slides*GALLERY_BYTES);
    if (board?.revision!==revision) return;
    const cards=[];
    for (let i=0;i<data.slides;i++) {
      const card=document.createElement("label"); card.className="slide";
      const canvas=document.createElement("canvas");
      canvas.setAttribute("aria-label","第 "+(i+1)+" 张预览");
      drawGallery(canvas,bytes.subarray(i*GALLERY_BYTES,(i+1)*GALLERY_BYTES));
      const label=document.createElement("span"), box=document.createElement("input"), badge=document.createElement("span"); label.className="caption";
      box.type="checkbox"; badge.className="playing";
      box.onchange=()=>{ if (box.checked) markedSlots.add(i); else markedSlots.delete(i); buttons(); };
      label.append(box,"第 "+(i+1)+" 张",badge); card.append(canvas,label); cards.push(card);
    }
    $("album").replaceChildren(...cards); albumSeen=revision; displayBadges();
    setText($("albumHint"),"点照片可选中，最多同时替换两张。");
  } catch { if (board?.revision===revision) setText($("albumHint"),"缩略图暂时未读到，正在重试。"); }
  finally { if (albumLoading===revision) albumLoading=""; buttons(); }
}
async function loadShown(data) {
  const sha=data.shown_sha;
  const size=data.shown_size;
  const key=sha+"/"+size;
  setText($("displayNote"),sha
    ? (data.shown_current ? "小板确认显示的画面。" : "上次确认显示的画面。")
    : "小板确认下一次刷新后会显示画面。");
  if (!sha) { $("displayThumb").hidden=$("enlarge").hidden=true; shownSeen=""; return; }
  if (size!==FRAME_BYTES && size!==THUMB_BYTES) return;
  if (key===shownSeen) { $("displayThumb").hidden=$("enlarge").hidden=false; return; }
  if (key===shownLoading) return;
  shownLoading=key; $("displayThumb").hidden=$("enlarge").hidden=true;
  try {
    const frame=await thumbnailBytes("/web/shown-preview?sha="+encodeURIComponent(sha),size,true);
    if (board?.shown_sha!==sha || board.shown_size!==size) return;
    drawPackedPreview($("displayThumb"),frame,size===FRAME_BYTES ? WIDTH : THUMB_WIDTH,size===FRAME_BYTES ? HEIGHT : THUMB_HEIGHT);
    shownSeen=key; $("displayThumb").hidden=$("enlarge").hidden=false;
  } catch { if (board?.shown_sha===sha) setText($("displayNote"),"画面暂时未读到，将自动重试。"); }
  finally { if (key===shownLoading) shownLoading=""; }
}
function photoItems(files) { return files.map(file=>({file,crop:null,frame:null,mode:"detail"})); }
async function prepareItem(item,image) {
  if (item.frame) return item.frame;
  image??=await decoded(item.file);
  if (item.frame) return item.frame;
  item.crop??=initialCrop(image);
  drawCrop(paint,image,item.crop);
  const frame=runConversion("frame",paint.getImageData(0,0,WIDTH,HEIGHT).data,item.mode);
  item.frame=frame;
  try { return await frame; }
  catch (error) { if (item.frame===frame) item.frame=null; throw error; }
}
function scheduleCropPaint() {
  if (paintQueued) return;
  paintQueued=true;
  requestAnimationFrame(()=>{
    paintQueued=false;
    if (activeImage&&!proofKey) drawCrop(ctx,activeImage,photos[selected].crop);
  });
}
function cropEdited() {
  proofKey=null;
  setText($("proof"),"看六色效果");
  scheduleCropPaint();
}
function adjustCrop(zoom,from,to=from) {
  if (!activeImage||working||photos[selected].crop.fit) return;
  const item=photos[selected], old=item.crop;
  const before=cropSize(activeImage,old.zoom);
  zoom=clamp(zoom,1,4);
  const after=cropSize(activeImage,zoom);
  const x=old.x+(from.x-.5)*before.width-(to.x-.5)*after.width;
  const y=old.y+(from.y-.5)*before.height-(to.y-.5)*after.height;
  item.crop={x:clamp(x,after.width/2,activeImage.naturalWidth-after.width/2),
    y:clamp(y,after.height/2,activeImage.naturalHeight-after.height/2),zoom};
  item.frame=null;
  $("zoom").value=zoom;
  setText($("scale"),Number(zoom.toFixed(2))+"×");
  $("zoomOut").disabled=zoom<=1; $("zoomIn").disabled=zoom>=4;
  cropEdited();
}
async function toggleProof() {
  if (proofKey) { cropEdited(); return; }
  const item=photos[selected];
  if (!activeImage||working||!item) return;
  const key={}; proofKey=key;
  setText($("proof"),"正在生成…");
  try {
    const frame=await prepareItem(item,activeImage);
    if (proofKey!==key) return;
    drawPackedPreview(previewCanvas,frame,WIDTH,HEIGHT);
    setText($("proof"),"返回裁剪");
  } catch {
    if (proofKey===key) { cropEdited(); setNotice("暂时无法生成六色预览，请重试。"); }
  }
}
async function preview() {
  const turn=++generation;
  const item=photos[selected];
  activeImage=null; pointers.clear(); cropEdited();
  $("inkMode").value=item?.mode??"detail";
  $("framing").value=item?.crop?.fit ? "fit" : "fill";
  setText($("cropHelp"),item?.crop?.fit ? "整张照片会按比例居中，空余部分留白。" : "拖动取景，双指或滚轮缩放。也可以用下方按钮微调。");
  $("draftJump").replaceChildren(...photos.map((entry,i)=>new Option((i+1)+" · "+entry.file.name,i)));
  $("draftJump").selectedIndex=selected;
  ctx.clearRect(0,0,WIDTH,HEIGHT);
  $("zoom").value=1;
  buttons();
  const slot=replacementDraft ? "替换屏上第 "+(replacementDraft.slots[selected]+1)+" 张 · " : "";
  setText($("photoLabel"),item ? slot+(selected+1)+" / "+photos.length+"  "+item.file.name : "");
  if (!item) return;
  try {
    const image=await decoded(item.file);
    if (turn!==generation) return;
    activeImage=image;
    item.crop??=initialCrop(image);
    $("zoom").value=item.crop.zoom;
    setText($("scale"),Number(item.crop.zoom.toFixed(2))+"×");
    scheduleCropPaint();
  } catch {
    if (turn===generation) setNotice("这张照片无法解码。请选 JPEG 或 PNG。");
  } finally { if (turn===generation) buttons(); }
}
function readStored(name) {
  try { return sessionStorage.getItem(name); } catch { return null; }
}
function saveStored(name,value) {
  try { if (value) sessionStorage.setItem(name,value); else sessionStorage.removeItem(name); } catch {}
}
function requestId() {
  return Array.from(crypto.getRandomValues(new Uint8Array(8)),byte=>byte.toString(16).padStart(2,"0")).join("");
}
function setWaiting(value) {
  waiting=value;
  saveStored("epaper.lastJob",value ? JSON.stringify(value) : null);
}
async function request(path, options={}) {
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),options.method==="POST"||options.body ? 50000 : 4000);
  try {
    const result=await fetch(path,{...options,mode:"same-origin",cache:"no-store",signal:controller.signal});
    if (!result.ok) { const err=new Error("HTTP "+result.status); err.http=result.status; throw err; }
    const receipt=await result.json();
    if (typeof receipt.boot_id!=="string"||!Number.isInteger(receipt.job)||!Number.isInteger(receipt.batch)) {
      throw new Error("回执无法读取");
    }
    return receipt;
  } finally { clearTimeout(timer); }
}
function renderStatus(data) {
  ++pollId;
  board=data; $("retry").hidden=true;
  if (layoutDraft && layoutDraft.revision!==data.revision && !working) {
    $("layout").close(); setText($("albumNotice"),"轮播已被更新，请按最新列表重新整理。");
  }
  setText($("albumSummary"),data.slides ? "共 "+data.slides+" 张，每 "+data.interval_minutes+" 分钟换图。可勾选 1 或 2 张替换。"
    : data.storage_ready ? "轮播列表为空。" : "轮播存储不可用。");
  loadAlbum(data); loadShown(data); displayBadges();
  let message="墨水屏在线";
  if (data.paused) message+=" · 刷新失败，轮播已暂停。照片已保留，可在管理轮播里重试。";
  else if (data.state==="refreshing") message+=" · 正在刷新，可继续选图和裁剪";
  else if (data.state==="accepted") message+=" · 已收到照片，等待刷新";
  else if (data.slides) message+=" · 正在轮播 "+data.slides+" 张";
  else if (data.state==="displayed") message+=" · 已显示";
  else if (data.state==="failed") message+=" · 刷新失败";
  if (!data.photo_ready) message+=" · 投图缓冲区不可用";
  if (data.storage_error) message+=" · 保存遇到问题";
  setText($("status"),message);
  const timing=data.state==="accepted" ? "约 "+formatTime(data.wait_ms)+" 后开始刷新。" : data.slides&&data.state!=="refreshing" ? "下一张是第 "+(1+data.next_slide)+" 张，约 "+formatTime(data.next_ms)+" 后换图。" : "";
  setText($("timing"),timing); $("timing").hidden=!timing||!!data.paused;
  if (waiting) {
    if (waiting.layout && waiting.boot===data.boot_id && data.batch_request===waiting.id && data.batch>waiting.seq) {
      setWaiting(null); setText($("albumNotice"),"轮播设置已保存。");
    } else if (data.boot_id!==waiting.boot) {
      if (waiting.layout) setText($("albumNotice"),"小艺已重启，请核对最新轮播列表。");
      setWaiting(null); setNotice("小板已经重启，上一次发送结果无法确认。");
    } else if (waiting.batch && data.batch_request===waiting.id && data.album_sha===waiting.sha && data.batch>waiting.seq && data.slides>0) {
      const notice=waiting.edit ? "所选照片已替换。" : "轮播已启动。可以关掉本页。";
      if (waiting.edit) finishReplacement();
      setWaiting(null); setNotice(notice);
    } else if (!waiting.batch && data.web_job===waiting.id && data.sha256===waiting.sha && data.job>waiting.seq && !data.slides) {
      if (data.state==="displayed" && data.refresh_ms>=10000 && data.refresh_ms<120000) {
        setWaiting(null); setNotice("小板已确认显示这张照片。");
      } else if (data.state==="failed") {
        setWaiting(null); setNotice("小板报告该照片刷新失败。");
      } else setNotice("小板已接收这张照片。正在等刷新回执。");
    } else if (Date.now()-waiting.sent>360000 || (waiting.batch ? data.batch : data.job)>waiting.seq+1) {
      if (waiting.layout) setText($("albumNotice"),"保存结果未确认，请核对最新列表。");
      setWaiting(null); setNotice("这次发送结果未确认。请先核对屏幕，再决定是否重投。");
    }
  }
  buttons();
}
async function readStatus() {
  if (working||document.hidden) return;
  const turn=++pollId;
  try { const data=await request("/web/status"); if (turn===pollId&&!working) renderStatus(data); }
  catch (err) {
    if (turn!==pollId||working) return;
    board=null; $("retry").hidden=false; $("timing").hidden=true;
    if (!$("displayThumb").hidden) setText($("displayNote"),"断线前最后确认的画面。");
    setText($("status"),err.http===403 ? "请连接家里的 Wi-Fi 后重新打开网页。" : "暂时读不到小板，仍可裁剪照片。");
    buttons();
  }
}
async function submit(batch, replacement=null) {
  if (working||waiting||!activeImage) return;
  const revision=replacement?.revision || board?.revision;
  const activeIndex=selected, image=activeImage;
  working=true; buttons();
  try {
    setNotice(batch ? "正在准备轮播照片。" : "正在发送照片。");
    let payload;
    if (batch) {
      payload=new Uint8Array(photos.length*FRAME_BYTES);
      for (let i=0;i<photos.length;i++) {
        payload.set(await prepareItem(photos[i],i===activeIndex ? image : null), i*FRAME_BYTES);
        setNotice("已准备 "+(i+1)+" / "+photos.length+" 张");
      }
    } else payload=await prepareItem(photos[activeIndex],image);
    const sha=await runConversion("hash",payload);
    const before=await request("/web/status");
    if (before.revision!==revision) {
      renderStatus(before); setNotice("轮播列表已变化，请重新确认后提交。"); return;
    }
    const id=requestId();
    setWaiting({batch,edit:!!replacement,sha,id,boot:before.boot_id,seq:batch?before.batch:before.job,sent:Date.now()});
    const path=replacement ? "/web/replace" : batch ? "/web/carousel" : "/web/frame";
    const headers={"Content-Type":"application/octet-stream","X-Frame-SHA256":sha,"X-Web-Request":id};
    headers["X-Album-Revision"]=revision;
    if (replacement) headers["X-Album-Slots"]=replacement.slots.join(",");
    else if (batch) headers["X-Rotation-Minutes"]=$("interval").value;
    renderStatus(await request(path,{method:"PUT",body:payload,headers}));
  } catch (err) {
    if (err.http && [400,401,403,409,503,507].includes(err.http)) {
      setWaiting(null);
      setNotice(err.http===409 ? "小板忙或列表有变化，请确认后再提交。"
        : err.http===507 ? "小板保存失败，旧列表仍可使用。" : "小板拒绝这次发送（"+err.http+"）。");
    } else if (waiting) setNotice("提交结果未确认。正在读回状态。");
    else setNotice("发送准备失败。请检查照片和 Wi-Fi。");
  } finally { working=false; buttons(); readStatus(); }
}
$("photos").onchange=()=>{
  chooseFiles($("photos"),false);
};
function chooseFiles(input,append) {
  const files=Array.from(input.files); input.value="";
  if (working||replacementDraft||!files.length) return;
  if (files.length+(append?photos.length:0)>12) {
    setNotice("一次最多选 12 张，之前的选择和裁剪已保留。"); return;
  }
  const items=photoItems(files);
  selected=append ? photos.length : 0;
  photos=append ? photos.concat(items) : items;
  setNotice(""); preview();
}
$("append").onchange=()=>chooseFiles($("append"),true);
$("draftJump").onchange=()=>{selected=Number($("draftJump").value);preview();};
function moveDraft(by) {
  const dest=selected+by;
  if (working||replacementDraft||dest<0||dest>=photos.length) return;
  [photos[dest],photos[selected]]=[photos[selected],photos[dest]];
  selected=dest; preview();
}
$("earlier").onclick=()=>moveDraft(-1);
$("later").onclick=()=>moveDraft(1);
$("removeDraft").onclick=()=>{
  if (working||replacementDraft) return;
  photos.splice(selected,1); selected=Math.max(0,Math.min(selected,photos.length-1));
  setNotice("已从待投照片中移出。"); preview();
};
$("prev").onclick=()=>{ if(selected>0) { selected--; preview(); } };
$("next").onclick=()=>{ if(selected+1<photos.length) { selected++; preview(); } };
const center={x:.5,y:.5};
$("zoomIn").onclick=()=>adjustCrop(photos[selected].crop.zoom+.1,center);
$("zoomOut").onclick=()=>adjustCrop(photos[selected].crop.zoom-.1,center);
$("pan").onclick=event=>{
  const b=event.target.closest("button[data-dx],button[data-dy]");
  if (b) adjustCrop(photos[selected].crop.zoom,center,{x:.5+.05*Number(b.dataset.dx||0),y:.5+.05*Number(b.dataset.dy||0)});
};
$("framing").onchange=()=>{
  if (!activeImage||working) return;
  photos[selected].crop.fit=$("framing").value==="fit";
  photos[selected].frame=null; cropEdited(); buttons();
  setText($("cropHelp"),photos[selected].crop.fit ? "整张照片会按比例居中，空余部分留白。" : "拖动取景，双指或滚轮缩放。也可以用下方按钮微调。");
};
$("retry").onclick=()=>readStatus();
$("enlarge").onclick=()=>{
  const from=$("displayThumb"), dest=$("largeCanvas");
  dest.width=from.width; dest.height=from.height;
  dest.getContext("2d").drawImage(from,0,0); $("lightbox").showModal();
};
$("closeLarge").onclick=()=>$("lightbox").close();
$("zoom").oninput=()=>adjustCrop(Number($("zoom").value),center);
$("inkMode").onchange=()=>{
  if (!activeImage||working) return;
  const item=photos[selected];
  item.mode=$("inkMode").value;
  item.frame=null;
  cropEdited();
  toggleProof();
};
$("resetCrop").onclick=()=>{
  if (!activeImage||working) return;
  photos[selected].crop=initialCrop(activeImage); photos[selected].frame=null;
  $("zoom").value=1; setText($("scale"),"1×"); cropEdited(); buttons();
};
$("proof").onclick=toggleProof;
function pointerPoint(event) {
  const bounds=previewCanvas.getBoundingClientRect();
  return {x:(event.clientX-bounds.left)/bounds.width,y:(event.clientY-bounds.top)/bounds.height};
}
function gesture() {
  const [a,b]=pointers.values();
  if (!b) return {point:a,distance:0};
  return {point:{x:(a.x+b.x)/2,y:(a.y+b.y)/2},distance:Math.hypot((a.x-b.x)*WIDTH,(a.y-b.y)*HEIGHT)};
}
previewCanvas.onpointerdown=event=>{
  if (!activeImage||working||photos[selected].crop.fit||event.button!==0||pointers.size>=2) return;
  previewCanvas.setPointerCapture(event.pointerId);
  pointers.set(event.pointerId,pointerPoint(event));
};
previewCanvas.onpointermove=event=>{
  if (!activeImage||working||!pointers.has(event.pointerId)) return;
  const before=gesture();
  pointers.set(event.pointerId,pointerPoint(event));
  const after=gesture();
  const zoom=photos[selected].crop.zoom*(before.distance>0 ? after.distance/before.distance : 1);
  adjustCrop(zoom,before.point,after.point);
};
previewCanvas.onlostpointercapture=event=>pointers.delete(event.pointerId);
previewCanvas.onpointerup=previewCanvas.onpointercancel=previewCanvas.onlostpointercapture;
previewCanvas.addEventListener("wheel",event=>{
  if (!activeImage||working) return;
  event.preventDefault();
  const steps=clamp(event.deltaY*(event.deltaMode===1 ? .05 : event.deltaMode===2 ? 1 : .002),-.5,.5);
  adjustCrop(photos[selected].crop.zoom*Math.exp(-steps),pointerPoint(event));
},{passive:false});
previewCanvas.onkeydown=event=>{
  if (!activeImage||working) return;
  const shifts={ArrowLeft:[-.04,0],ArrowRight:[.04,0],ArrowUp:[0,-.04],ArrowDown:[0,.04]};
  const shift=shifts[event.key];
  if (!shift) return;
  event.preventDefault();
  adjustCrop(photos[selected].crop.zoom,center,{x:.5+shift[0],y:.5+shift[1]});
};
$("send").onclick=()=>{
  if (!board) return;
  if (!board.slides || confirm("投这张照片会结束轮播，并清空已保存的 "+board.slides+" 张轮播照片。继续投图？")) submit(false);
};
$("start").onclick=()=>{
  if (replacementDraft) { submit(true,replacementDraft); return; }
  if (!board?.slides || confirm("用待投的 "+photos.length+" 张照片替换屏上全部 "+board.slides+" 张轮播照片？")) submit(true);
};
function drawOrder(focusIndex,action) {
  const draft=layoutDraft, list=$("orderList");
  const rows=draft.slots.map((slot,index)=>{
    const row=document.createElement("div"); row.className="orderRow";
    const canvas=document.createElement("canvas");
    canvas.width=GALLERY_WIDTH; canvas.height=GALLERY_HEIGHT;
    canvas.getContext("2d").drawImage(draft.thumbs[slot],0,0);
    const label=document.createElement("span"); label.textContent="第 "+(index+1)+" 张";
    row.append(canvas,label);
    for (const [key,text,disabled] of [["up","上移",index===0],["down","下移",index===draft.slots.length-1],["remove","移出",draft.slots.length<=2]]) {
      const button=document.createElement("button");
      button.textContent=text; button.dataset.move=key; button.disabled=disabled;
      button.setAttribute("aria-label",text+"第 "+(index+1)+" 张"); row.append(button);
    }
    return row;
  });
  list.replaceChildren(...rows);
  if (focusIndex!==undefined) {
    const row=rows[Math.min(focusIndex,rows.length-1)];
    const target=row.querySelector('[data-move="'+action+'"]:not(:disabled)')||row.querySelector('button:not(:disabled)');
    target?.focus();
  }
  const removed=draft.thumbs.length-draft.slots.length;
  setText($("layoutNote"),removed ? "保存后会从轮播移出 "+removed+" 张，取消可放弃本次修改。" : "用上移、下移调整播放顺序，至少保留两张。");
  layoutButtons();
}
function layoutButtons() {
  const d=layoutDraft;
  if (!d) return;
  const changed=d.slots.length!==d.thumbs.length||d.slots.some((s,i)=>s!==i)||Number($("layoutInterval").value)!==d.interval;
  $("saveLayout").disabled=working||!!waiting||!changed||!board||board.revision!==d.revision||["accepted","refreshing"].includes(board.state);
}
$("organize").onclick=()=>{
  if (!board?.slides || working||waiting||albumSeen!==board.revision) return;
  const thumbs=Array.from($("album").querySelectorAll("canvas"));
  if (thumbs.length!==board.slides) return;
  layoutDraft={revision:board.revision,interval:board.interval_minutes,slots:thumbs.map((_,i)=>i),thumbs};
  $("layoutInterval").value=String(layoutDraft.interval); drawOrder();
  $("layout").showModal();
};
$("orderList").onclick=e=>{
  const b=e.target.closest("button");
  if (!b||working||!layoutDraft) return;
  const slots=layoutDraft.slots, i=Array.from($("orderList").children).indexOf(b.parentElement);
  const op=b.dataset.move;
  if (op==="remove") { if(slots.length<=2) return; slots.splice(i,1); drawOrder(i,op); }
  else {
    const j=i+(op==="up"?-1:1);
    if (j<0||j>=slots.length) return;
    [slots[i],slots[j]]=[slots[j],slots[i]]; drawOrder(j,op);
  }
};
$("layoutInterval").onchange=layoutButtons;
$("cancelLayout").onclick=()=>$("layout").close();
$("layout").oncancel=e=>{ if(working) e.preventDefault(); };
$("layout").onclose=()=>{layoutDraft=null;};
$("saveLayout").onclick=async ()=>{
  const d=layoutDraft;
  if (!d||working||waiting||!board||board.revision!==d.revision) return;
  const removed=d.thumbs.length-d.slots.length;
  if (removed && !confirm("从轮播移出 "+removed+" 张，保留 "+d.slots.length+" 张？")) return;
  working=true; ++pollId; layoutButtons();
  $("layoutInterval").disabled=$("cancelLayout").disabled=true;
  $("orderList").querySelectorAll("button").forEach(b=>b.disabled=true);
  setText($("layoutNote"),"正在保存到小艺…");
  const id=requestId();
  try {
    setWaiting({layout:true,batch:true,id,boot:board.boot_id,seq:board.batch,sent:Date.now()});
    const result=await request("/web/arrange",{method:"POST",headers:{"X-Album-Revision":d.revision,
      "X-Web-Request":id,"X-Rotation-Minutes":$("layoutInterval").value,"X-Album-Slots":d.slots.join(",")}});
    renderStatus(result);
  } catch (err) {
    if (err.http && [400,403,409,503,507].includes(err.http)) {
      setWaiting(null); setText($("albumNotice"),err.http===409 ? "列表已变化或小艺正在刷新，请按最新状态再整理。" : "轮播设置保存失败，之前的列表仍在。");
    } else setText($("albumNotice"),"正在核对保存结果，请稍候。");
  } finally {
    $("layout").close(); working=false; $("layoutInterval").disabled=$("cancelLayout").disabled=false;
    buttons(); readStatus();
  }
};
$("replace").onclick=()=>{
  if (!board?.slides || !markedSlots.size) return;
  replacementPlan={revision:board.revision,slots:[...markedSlots].sort((a,b)=>a-b)};
  $("replacementFiles").value=""; $("replacementFiles").click();
};
$("replacementFiles").onchange=()=>{
  const plan=replacementPlan;
  replacementPlan=null;
  if (!plan) return;
  const files=Array.from($("replacementFiles").files);
  if (files.length!==plan.slots.length) { setText($("albumNotice"),"请按替换位置的顺序选择 "+plan.slots.length+" 张新照片。"); return; }
  setText($("albumNotice"),"");
  replacementDraft={...plan,previousPhotos:photos,previousIndex:selected};
  photos=photoItems(files); selected=0;
  setNotice("调整新照片的裁剪，然后确认替换。");
  preview(); $("composer").scrollIntoView({block:"start"});
};
function finishReplacement() {
  if (!replacementDraft) return;
  ({previousPhotos:photos,previousIndex:selected}=replacementDraft);
  replacementDraft=null; preview();
}
$("cancelReplace").onclick=()=>{ finishReplacement(); setNotice("已退出替换编辑。"); };
$("swapReplacement").onclick=()=>{
  if (!replacementDraft||photos.length!==2||working||waiting) return;
  photos.reverse(); selected=1-selected; preview();
  setNotice("两张照片的替换位置已对调。");
};
$("resume").onclick=async ()=>{
  if (!board?.paused||working||waiting) return;
  const revision=board.revision;
  working=true; buttons();
  try {
    renderStatus(await request("/web/resume",{method:"POST",headers:{"X-Album-Revision":revision}}));
    setText($("albumNotice"),"轮播已恢复，小艺会在刷新保护间隔结束后重试。");
  } catch { setText($("albumNotice"),"正在核对轮播状态。如果仍显示暂停，可以再次重试。"); }
  finally { working=false; buttons(); readStatus(); }
};
$("stop").onclick=async ()=>{
  if (!board?.slides || !confirm("清空全部 "+board.slides+" 张轮播照片？屏幕会继续显示当前画面。")) return;
  const revision=board.revision;
  working=true; buttons();
  try {
    renderStatus(await request("/web/stop",{method:"POST",headers:{"X-Album-Revision":revision}}));
    setNotice("轮播列表已清空。"); setText($("albumNotice"),"轮播列表已清空，当前画面继续显示。");
  } catch (error) { setText($("albumNotice"),error.http===409 ? "轮播列表已变化，请重新确认。"
    : error.http===507 ? "清空未保存，轮播列表已保留。" : "暂时无法确认清空结果，正在读回状态。"); }
  finally { working=false; buttons(); readStatus(); }
};
try { waiting=JSON.parse(readStored("epaper.lastJob")); } catch { waiting=null; }
saveStored("epaper.familyCode",null);
if (new URLSearchParams(location.hash.slice(1)).has("k")) {
  history.replaceState(null,"",location.pathname);
}
buttons(); readStatus(); setInterval(readStatus,5000);
document.addEventListener("visibilitychange",()=>{ if (!document.hidden) readStatus(); });
</script>
</html>)familyweb";
