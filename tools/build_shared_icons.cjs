/** Shared code-native artwork: one 16:10 monitor silhouette for all states.
 * Run: node tools/build_shared_icons.cjs <absolute path to sharp>
 * No app runtime dependency. SVG sources and each ICO frame are rendered directly.
 */
const fs = require('fs');
const path = require('path');
const sharp = require(process.argv[2] || 'sharp');
const out = path.join(__dirname, '../assets/icons/shared-16x10');
const sizes = [16,20,24,32,40,48,64,96,128,256];
const states = ['active','hdr','disabled','disabled-hdr','application','application-hdr'];
const labels = ['Active','Active - HDR','Paused','Paused - HDR','Application','Application - HDR'];
fs.mkdirSync(out,{recursive:true});
function svg(name,n) {
    const paused=name.startsWith('disabled'), hdr=name.includes('hdr'), app=name.startsWith('application');
    const top=Math.round(n*.11), h=n*10/16, bottom=top+h;
    const b=Math.max(1,n*.035), radius=Math.max(1,n*.045);
    const left=b, right=n-b, st=top+b, sb=bottom-b;
    const x=v=>left+(right-left)*v, y=v=>st+(sb-st)*v;
    const stroke=Math.max(1.25,n*.064);
    const cx=x(.16), cy=y(.23), r=n*.044, reach=n*(n<=20?.105:.095);
    let sun='';
    if(hdr) {
        const rays=[];
        for(let i=0;i<8;i++) {
            if(n<=20 && i%2)continue;
            const angle=i*Math.PI/4;
            rays.push(`<path d="M${cx+Math.cos(angle)*r*1.6} ${cy+Math.sin(angle)*r*1.6} L${cx+Math.cos(angle)*reach} ${cy+Math.sin(angle)*reach}"/>`);
        }
        sun=`<g fill="#ffda24" stroke="#ffda24" stroke-width="${Math.max(.8,n*.024)}" stroke-linecap="round">${rays.join('')}<circle cx="${cx}" cy="${cy}" r="${r}" stroke="none"/></g>`;
    }
    let pause='';
    if(paused) {
        const bw=Math.max(1,n*.045), gap=Math.max(1,n*.034), ph=n*.145;
        const px=x(.77), py=y(.61);
        pause=`<g fill="#c2c6cc"><rect x="${px}" y="${py}" width="${bw}" height="${ph}" rx="${bw*.2}"/><rect x="${px+bw+gap}" y="${py}" width="${bw}" height="${ph}" rx="${bw*.2}"/></g>`;
    }
    let badge='';
    if(app) {
        const cx=x(.88), cy=y(.79), r=n*.072;
        badge=`<path d="M${cx} ${cy-r} L${cx+r} ${cy} L${cx} ${cy+r} L${cx-r} ${cy} Z" fill="#55c7ff" stroke="#0a1119" stroke-width="${Math.max(.7,n*.012)}"/>`;
    }
    return `<svg xmlns="http://www.w3.org/2000/svg" width="${n}" height="${n}" viewBox="0 0 ${n} ${n}">
<defs><linearGradient id="curve"><stop stop-color="#00dce9"/><stop offset=".32" stop-color="#168eff"/><stop offset=".62" stop-color="#ed43ef"/><stop offset="1" stop-color="#ffe923"/></linearGradient><linearGradient id="case" x2="0" y2="1"><stop stop-color="#727a83"/><stop offset="1" stop-color="#535b65"/></linearGradient><clipPath id="screen"><rect x="${left}" y="${st}" width="${right-left}" height="${sb-st}" rx="${Math.max(.4,radius-b*.5)}"/></clipPath></defs>
<path d="M${n*.43} ${bottom-b} H${n*.57} L${n*.59} ${n*.82} H${n*.41} Z" fill="#616973"/>
<rect x="${n*.30}" y="${n*.81}" width="${n*.40}" height="${Math.max(1,n*.075)}" rx="${Math.max(.5,n*.025)}" fill="#69717c"/>
<rect x="0" y="${top}" width="${n}" height="${h}" rx="${radius}" fill="#9299a1"/>
<rect x="${Math.max(.35,n*.006)}" y="${top+Math.max(.35,n*.006)}" width="${n-2*Math.max(.35,n*.006)}" height="${h-2*Math.max(.35,n*.006)}" rx="${Math.max(.5,radius-n*.006)}" fill="url(#case)"/>
<rect x="${left}" y="${st}" width="${right-left}" height="${sb-st}" rx="${Math.max(.4,radius-b*.5)}" fill="#090b0e"/>
<g clip-path="url(#screen)"><path d="M${x(.065)} ${y(.84)} ${paused?`L${x(.92)} ${y(.19)}`:`C${x(.48)} ${y(.84)} ${x(.49)} ${y(.19)} ${x(.92)} ${y(.19)}`}" fill="none" stroke="${paused?'#a9afb7':'url(#curve)'}" stroke-width="${stroke}" stroke-linecap="round"/>${sun}${pause}${badge}</g>
</svg>`;
}
function ico(frames) {
    const header=Buffer.alloc(6+16*frames.length);header.writeUInt16LE(1,2);header.writeUInt16LE(frames.length,4);
    let offset=header.length;
    frames.forEach(({n,png},i)=>{
        const at=6+i*16;header[at]=n===256?0:n;header[at+1]=n===256?0:n;
        header.writeUInt16LE(1,at+4);header.writeUInt16LE(32,at+6);header.writeUInt32LE(png.length,at+8);header.writeUInt32LE(offset,at+12);offset+=png.length;
    });
    return Buffer.concat([header,...frames.map(f=>f.png)]);
}
(async()=>{
    const diamond=n=>`<svg xmlns="http://www.w3.org/2000/svg" width="${n}" height="${n}" viewBox="0 0 ${n} ${n}"><path d="M${n*.5} ${n*.08} L${n*.92} ${n*.5} L${n*.5} ${n*.92} L${n*.08} ${n*.5} Z" fill="none" stroke="#a4a4a4" stroke-width="${Math.max(1,n*.05)}" stroke-linejoin="round"/></svg>`;
    fs.writeFileSync(path.join(out,'profile-diamond.svg'),diamond(512));
    await sharp(Buffer.from(diamond(512))).png().toFile(path.join(out,'profile-diamond.png'));
    const diamondFrames=[];
    for(const n of sizes)diamondFrames.push({n,png:await sharp(Buffer.from(diamond(n))).png().toBuffer()});
    fs.writeFileSync(path.join(out,'profile-diamond.ico'),ico(diamondFrames));
    const manifest={bodyRatio:'16:10 (excluding stand)',fullWidth:true,sizes,states:{}};
    for(const name of states) {
        const source=svg(name,512);fs.writeFileSync(path.join(out,name+'.svg'),source);
        await sharp(Buffer.from(source)).png().toFile(path.join(out,name+'.png'));
        const frames=[];manifest.states[name]={};
        for(const n of sizes) {
            const png=await sharp(Buffer.from(svg(name,n))).png().toBuffer();frames.push({n,png});
            const {data,info}=await sharp(png).ensureAlpha().raw().toBuffer({resolveWithObject:true});
            let l=n,t=n,r=0,b=0;
            for(let y=0;y<n;y++)for(let x=0;x<n;x++)if(data[(y*n+x)*info.channels+3]>=128){l=Math.min(l,x);t=Math.min(t,y);r=Math.max(r,x+1);b=Math.max(b,y+1);}
            if(l!==0||r!==n)throw Error(`${name} ${n}: width not filled`);
            manifest.states[name][n]={visible:[l,t,r,b],bodyHeight:n*10/16};
        }
        fs.writeFileSync(path.join(out,name+'.ico'),ico(frames));
        console.log(name+': SVG + PNG + '+sizes.length+' directly rendered ICO frames');
    }
    for(const n of sizes) {
        const reference=manifest.states.active[n].visible.join(',');
        for(const name of states)if(manifest.states[name][n].visible.join(',')!==reference)throw Error('State alignment mismatch');
    }
    fs.writeFileSync(path.join(out,'measurements.json'),JSON.stringify(manifest,null,2));
    let rows='';
    for(let i=0;i<states.length;i++) {
        let cells='';
        for(const n of [16,20,24,32,48,64]) {
            const png=await sharp(Buffer.from(svg(states[i],n))).png().toBuffer();
            cells+=`<figure><img width="${n}" height="${n}" src="data:image/png;base64,${png.toString('base64')}"/><figcaption>${n}px</figcaption></figure>`;
        }
        rows+=`<section><h2>${labels[i]}</h2><div class="samples">${cells}</div></section>`;
    }
    fs.writeFileSync(path.join(out,'comparison.html'),`<!doctype html><meta charset="utf-8"><title>MonitorMage - shared 16:10 icons</title><style>body{background:#202124;color:#edf0f5;font:16px 'Segoe UI',sans-serif;margin:28px}body.light{background:#eee;color:#222}h1{font-size:24px}h2{font-size:16px;width:180px}section{display:flex;align-items:center;border-bottom:1px solid #6665;padding:12px 0}.samples{display:flex;gap:36px;align-items:center}figure{margin:0;min-width:50px;text-align:center}img{display:block;margin:auto}figcaption{font-size:12px;margin-top:8px;opacity:.7}button{font:inherit;padding:7px 12px}</style><h1>MonitorMage - shared 16:10 icons</h1><p>Every state shares one full-width monitor silhouette. Actual ICO artwork at each size. View at 100% zoom.</p><button onclick="document.body.classList.toggle('light')">Light / dark</button>${rows}`);
    // A scalable contact sheet, rendered from the same native geometry.
    let board='<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="690"><rect width="1000" height="690" fill="#202124"/><text x="24" y="38" fill="#eef0f4" font-family="Segoe UI" font-size="23">MonitorMage - full-width 16:10 monitor</text>';
    for(let i=0;i<states.length;i++) {
        const y=68+i*100;
        board+=`<text x="24" y="${y+45}" fill="#eef0f4" font-family="Segoe UI" font-size="16">${labels[i]}</text>`;
        for(const [x,n] of [[215,16],[285,20],[355,24],[435,32],[525,48],[635,64],[755,96]]) {
            const sample=await sharp(Buffer.from(svg(states[i],n))).png().toBuffer();
            board+=`<image x="${x}" y="${y+(96-n)/2}" width="${n}" height="${n}" href="data:image/png;base64,${sample.toString('base64')}"/>`;
            board+=`<text x="${x}" y="${y+98}" fill="#a5acb7" font-family="Segoe UI" font-size="11">${n}px</text>`;
        }
    }
    board+='</svg>';await sharp(Buffer.from(board)).png().toFile(path.join(out,'comparison.png'));
})().catch(e=>{console.error(e);process.exit(1)});
