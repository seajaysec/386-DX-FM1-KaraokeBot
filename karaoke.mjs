/* SPDX-License-Identifier: GPL-3.0-only */
// Browser-only Standard MIDI File reader. Metadata is data, never executable text.
export function readMidi(input){
 const a=new Uint8Array(input);if(a.length>5000000)throw Error('MIDI file exceeds 5 MB.');let pos=0;
 const need=n=>{if(pos+n>a.length)throw Error('Truncated MIDI file.');};
 const u8=()=>{need(1);return a[pos++];},u16=()=>u8()*256+u8(),u32=()=>u16()*65536+u16(),tag=()=>String.fromCharCode(u8(),u8(),u8(),u8());
 if(tag()!=='MThd')throw Error('Choose a Standard MIDI .mid or .kar file.');const h=u32();if(h<6)throw Error('Invalid MIDI header.');need(h);const format=u16(),count=u16(),ppqn=u16();pos+=h-6;
 if(format>1||!count||count>128||!ppqn||(ppqn&32768))throw Error('Use MIDI format 0 or 1 with a musical tick division.');
 const tracks=[],tempos=[],meters=[];let events=0,maxTick=0;
 const decoder=new TextDecoder('windows-1252');
 for(let id=0;id<count;id++){
  if(tag()!=='MTrk')throw Error('Missing MIDI track.');const size=u32();need(size);const end=pos+size;let tick=0,running=0;const notes=[],texts=[],held=new Map(),programs=new Map();let name='',bends=0,controllers=0;
  const byte=()=>{if(pos>=end)throw Error('Truncated MIDI event.');return u8();};
  const vlq=()=>{let x=0;for(let i=0;i<4;i++){const b=byte();x=x*128+(b&127);if(!(b&128))return x;}throw Error('Invalid MIDI variable length.');};
  while(pos<end){if(++events>200000)throw Error('MIDI has too many events.');tick+=vlq();maxTick=Math.max(maxTick,tick);let st=byte();if(st<128){if(!running)throw Error('Invalid running status.');pos--;st=running;}
   if(st===255){running=0;const type=byte(),len=vlq();if(pos+len>end)throw Error('Truncated MIDI metadata.');const data=a.slice(pos,pos+len);pos+=len;
    if(type===3)name=decoder.decode(data);
    if(type===1||type===5)texts.push({tick,text:decoder.decode(data),kind:type});
    if(type===81&&len===3)tempos.push({tick,tempo:data[0]*65536+data[1]*256+data[2]});
    if(type===88&&len>=2)meters.push({tick,numerator:data[0],denominator:2**data[1]});
    if(type===47){pos=end;break;}continue;
   }
   if(st===240||st===247){running=0;const len=vlq();if(pos+len>end)throw Error('Truncated MIDI SysEx.');pos+=len;continue;}
   if(st<128||st>=240)throw Error('Unsupported MIDI system event.');running=st;const op=st>>4,ch=st&15,x=byte(),y=op===12||op===13?0:byte();if(x>127||y>127)throw Error('Invalid MIDI data byte.');
   if(op===12)programs.set(ch,x);if(op===14)bends++;if(op===11&&![0,7,10,32,91,93,121].includes(x))controllers++;
   const key=ch*128+x;
   if(op===9&&y){const queue=held.get(key)||[];queue.push({tick,note:x,velocity:y,channel:ch,program:programs.get(ch)||0});held.set(key,queue);}
   else if(op===8||op===9&&!y){const queue=held.get(key);if(queue?.length){const n=queue.shift();notes.push({...n,end:Math.max(tick,n.tick+1)});}}
  }
  for(const queue of held.values())for(const n of queue)notes.push({...n,end:Math.max(tick,n.tick+1),unfinished:true});
  notes.sort((a,b)=>a.tick-b.tick||a.note-b.note);tracks.push({id,name:name||`Track ${id+1}`,notes,texts,bends,controllers});
 }
 // Mixed-channel SMF0 tracks expose their parts independently. Keep lyric
 // metadata on the original track instead of duplicating it for every channel.
 const originals=[...tracks];
 const programs={25:'Steel guitar',32:'Acoustic bass',33:'Electric bass',73:'Flute',82:'Synth lead',105:'Banjo',110:'Fiddle'};
 for(const t of originals){const channels=[...new Set(t.notes.map(n=>n.channel))].sort((a,b)=>a-b);if(channels.length<2)continue;const notes=t.notes;t.notes=[];
  for(const channel of channels){const part=notes.filter(n=>n.channel===channel),program=part[0]?.program||0;tracks.push({id:tracks.length,name:`Channel ${channel+1} · ${channel===9?'Drums':programs[program]||'Instrument'}`,notes:part,texts:[],bends:t.bends,controllers:t.controllers,sourceTrack:t.id,channel});}
 }
 const streams=[];
 for(const t of tracks)for(const kind of [5,1]){const raw=t.texts.filter(x=>x.kind===kind),sung=raw.filter(x=>x.text.trim()&&!isCredit(x.text));if(sung.length)streams.push({id:`${t.id}:${kind}`,track:t.id,kind,name:`${t.name} · ${kind===5?'lyrics':'karaoke text'}`,events:raw.filter(x=>!isCredit(x.text))});}
 if(!streams.length)throw Error('No timed karaoke lyrics were found.');
 tempos.sort((a,b)=>a.tick-b.tick);meters.sort((a,b)=>a.tick-b.tick);
 const lyric=streams.reduce((best,s)=>{const events=s.events.filter(e=>e.text.trim()&&!isCredit(e.text)&&!/^[\s/\\]*\.[\s]*$/.test(e.text)),ticks=new Set(events.map(e=>e.tick));const score=ticks.size*(s.kind===5?1.2:1);return score>best.score?{stream:s,score}:best;},{stream:streams[0],score:-1}).stream;
 const ticks=lyric.events.filter(e=>e.text.trim()&&!isCredit(e.text)).map(e=>e.tick);
 const rating=t=>{const ns=t.notes.filter(n=>n.channel!==9);if(!ns.length)return -Infinity;let aligned=0,poly=0;for(let i=0;i<ns.length;i++){if(ticks.some(k=>Math.abs(k-ns[i].tick)<=ppqn/4))aligned++;if(i&&ns[i].tick<ns[i-1].end)poly++;}const mean=ns.reduce((n,e)=>n+e.note,0)/ns.length;return aligned/ns.length*100-poly/ns.length*150-(mean<48?30:mean>84?20:0)+(/melody|voice/i.test(t.name)?100:0);};
 const melody=tracks[lyric.track].notes.length?lyric.track:tracks.reduce((best,t)=>rating(t)>best.score?{id:t.id,score:rating(t)}:best,{id:0,score:-Infinity}).id;
 const metadata=tracks.flatMap(t=>t.texts).find(e=>e.text.startsWith('@T'))?.text.slice(2).trim(),trackTitle=originals.find(t=>!/^Track \d+$/i.test(t.name)&&!/^\d+DEMO$/i.test(t.name))?.name;
 const lyricText=lyric.events.map(e=>e.text.replace(/[\r\n/\\]/g,'')).join('');const title=metadata||trackTitle||(/whiskey in the jar/i.test(lyricText)?'Whiskey in the Jar':undefined);
 return {tracks,streams,tempos,meters,ppqn,maxTick,lyric:lyric.id,melody,title};
}
const ascii=s=>s.normalize('NFKD').replace(/[\u0300-\u036f]/g,'').replace(/[‘’]/g,"'").replace(/[“”]/g,'"').replace(/[–—]/g,'-');
const isCredit=text=>text.replace(/^[\s/\\]+/,'').startsWith('@')||/^(?:[\s/\\]*(?:international rights|all rights reserved|do not duplicate|not for (?:rental|broadcast)|or transmission|gm device|(?:sequenced|arranged|composed|written|performed|produced|transcribed|programmed|lyrics|music)\s+by|(?:sequence|arrangement|composer|author|artist|attribution|credits?|keyboards?|drums(?:\s*&\s*backup)?|backup|bass|piano)\s*:|copyright|\(c\)|©))/i.test(text);
export function lyricLines(stream){
 const raw=[];let cur={text:'',chunks:[]};const finish=()=>{cur.text=cur.text.trim();if(cur.chunks.length){cur.chunks[0].text=cur.chunks[0].text.trimStart();cur.chunks.at(-1).text=cur.chunks.at(-1).text.trimEnd();}if(/[A-Za-z0-9]/.test(cur.text)&&cur.chunks.length)raw.push(cur);cur={text:'',chunks:[]};};
 for(const e of stream.events){if(isCredit(e.text))continue;const parts=ascii(e.text).replace(/\\r/g,'\r').replace(/\\n/g,'\n').split(/([\r\n/\\])/);
  for(const part of parts){if(!part)continue;if(/^[\r\n/\\]$/.test(part)){if(part==='/'||part==='\\'||!cur.text||/\s$/.test(cur.text))finish();continue;}if(/[^\x20-\x7e]/.test(part))throw Error('Lyrics contain unsupported characters. Edit a copy into English ASCII before importing.');if(part.trim()){cur.chunks.push({tick:e.tick,text:part});cur.text+=part;}else {cur.text+=part;if(cur.chunks.length)cur.chunks.at(-1).text+=part;}}
 }finish();
 // Long karaoke rows split at complete-word boundaries, preserving each
 // original chunk's tick. Never split a word merely because its display wrapped.
 const lines=[];
 for(const row of raw){let line={text:'',chunks:[]},word={text:'',chunks:[]};const emit=()=>{if(line.text.trim()){line.text=line.text.trim();lines.push(line);}line={text:'',chunks:[]};};const addWord=()=>{if(!word.text)return;if(line.text&&line.text.length+word.text.trimEnd().length>63)emit();line.text+=word.text;line.chunks.push(...word.chunks);word={text:'',chunks:[]};};
  for(const chunk of row.chunks){word.text+=chunk.text;word.chunks.push(chunk);if(/\s$/.test(chunk.text))addWord();}addWord();emit();
 }
 return lines;
}
const blank=()=>({notes:[0,0,0,0],count:0,velocity:100,gate:100,drums:0,enabled:0});
const drumMap=new Map([[35,0],[36,0],[38,1],[40,1],[41,2],[43,2],[45,3],[47,3],[48,4],[50,4],[37,5],[39,6],[56,7],[49,8],[51,8],[52,8],[55,8],[57,8],[59,8],[46,9],[42,10],[44,10]]);
export function planMidi(m,opts={}){
 const report=[],errors=[],stream=m.streams.find(s=>s.id===(opts.lyric||m.lyric));if(!stream)throw Error('Choose a lyric source.');const melody=m.tracks[Number(opts.melody??m.melody)];if(!melody?.notes.length)throw Error('Choose a melody track with notes.');
 const tempo=m.tempos[0]?.tempo||500000,bpm=Number(opts.bpm||Math.round(60000000/tempo));if(!Number.isInteger(bpm)||bpm<40||bpm>240)errors.push('Tempo must be 40–240 BPM.');if(m.tempos.some(t=>t.tempo!==tempo))errors.push('Changing tempo: export a constant-tempo MIDI first.');
 const meter=m.meters[0]||{numerator:4,denominator:4};const steps=meter.numerator===3&&meter.denominator===4?12:16;if(![3,4].includes(meter.numerator)||meter.denominator!==4||m.meters.some(t=>t.numerator!==meter.numerator||t.denominator!==meter.denominator))errors.push('Use constant 3/4 or 4/4 meter.');
 const q=t=>Math.round(t*4/m.ppqn),bars=Math.ceil((q(m.maxTick)+1)/steps);if(bars>128&&!opts._unbounded)errors.push(`${bars} bars exceeds 128; split the song into sections.`);
 const phrases=lyricLines(stream),lines=[],notes=[];let overlap=0,ornaments=0,legato=0,unmatched=0,ties=0,maxRounding=0;
 for(const p of phrases){let line=lines.indexOf(p.text);if(line<0){line=lines.length;lines.push(p.text);}if(p.text.length>63)errors.push(`Line exceeds 63 characters: ${p.text.slice(0,24)}…`);
  const next=phrases[phrases.indexOf(p)+1]?.chunks[0]?.tick??Infinity;let syllable=-1,lastChunk=-1;
  const source=melody.notes.filter(n=>n.channel!==9&&n.tick>=p.chunks[0].tick-m.ppqn/4&&q(n.tick)<q(next));
  for(const n of source){let chunk=-1;for(let j=0;j<p.chunks.length;j++)if(q(p.chunks[j].tick)<=q(n.tick))chunk=j;
   if(chunk<0){unmatched++;continue;}if(chunk!==lastChunk){syllable=chunk;lastChunk=chunk;}
   const at=q(n.tick),length=Math.max(1,q(n.end)-at);maxRounding=Math.max(maxRounding,Math.abs(at-n.tick*4/m.ppqn));const previous=notes.at(-1),tie=!!previous&&previous.line===line&&previous.syllable===syllable&&chunk===lastChunk&&at===previous.at+previous.length;
   if(previous&&at<previous.at+previous.length){if(at>previous.at&&opts.trim!==false){previous.length=at-previous.at;legato++;}else if(at===previous.at&&opts.ornaments){ornaments++;continue;}else {overlap++;continue;}}
   notes.push({at,length,note:n.note+Number(opts.transpose||0)+12*Number(opts.octave||0),line,syllable,tie});if(tie)ties++;
  }
 }
 if(lines.length>32&&!opts._unbounded)errors.push(`${lines.length} unique lyric lines exceeds 32; split the song.`);if(notes.length>512&&!opts._unbounded)errors.push(`${notes.length} vocal notes exceeds 512; split the song.`);if(!notes.length)errors.push('No melody notes aligned with these lyrics.');if(overlap)errors.push(`${overlap} overlapping vocal notes; choose a monophonic melody track.`);
 for(const n of notes){if(n.note<36||n.note>84){errors.push('Vocal notes must fit C2–C6. Adjust transpose.');break;}if(n.length>255){errors.push('A vocal note is longer than 255 sixteenths.');break;}if(n.syllable>63){errors.push('A line has more than 64 lyric syllables.');break;}}
 for(let i=0;i<notes.length;i++){if(notes[i].tie)continue;let j=i+1;while(notes[j]?.tie)j++;const group=notes.slice(i,j),seconds=group.reduce((s,n)=>s+n.length,0)*60/(bpm*4);if(!opts._unbounded&&group.length>16)errors.push('A vowel has more than 16 tied notes; split its continuation.');if(!opts._unbounded&&seconds>4.5&&group.some(n=>n.note!==group[0].note))errors.push('A changing-pitch tied vowel exceeds 4.5 seconds; split its continuation.');}
 const assignments=opts.assignments||Object.fromEntries(m.tracks.filter(t=>t.id!==melody.id&&t.notes.length).map(t=>[t.id,t.notes.every(n=>n.channel===9)?'drums':/bass/i.test(t.name)?'bass':'omit']));
 const arrangement=Array.from({length:opts._unbounded?bars:Math.min(bars,128)},()=>Array.from({length:3},()=>Array.from({length:16},blank)));let dropped=0,collisions=0,unmapped=0;
 for(const t of m.tracks){const dest=assignments[t.id];if(!['melody','bass','drums'].includes(dest))continue;const bus={melody:0,bass:1,drums:2}[dest];for(const n of t.notes){const at=q(n.tick),bar=Math.floor(at/steps);if(!arrangement[bar])continue;const s=arrangement[bar][bus][at%steps];s.velocity=Math.max(s.enabled?s.velocity:0,n.velocity);s.enabled=1;const dur=Math.max(1,q(n.end)-at);s.gate=100+Math.min(16,dur);if(dest==='drums'){const d=drumMap.get(n.note);if(d===undefined){unmapped++;continue;}s.drums|=1<<d;}else {const pitch=n.note+12*Number(opts.backingOctaves?.[bus]||0);if(pitch<0||pitch>127){errors.push(`${dest} octave shift exceeds MIDI pitch range.`);continue;}if(!s.notes.slice(0,s.count).includes(pitch)){if(s.count<(dest==='bass'?1:4))s.notes[s.count++]=pitch;else {collisions++;if(dest==='bass'&&pitch<s.notes[0])s.notes[0]=pitch;}}}if(dur>16)dropped++;}}
 // Select common backing bars, then spread representatives across musical
 // differences. The review exposes every non-exact reuse; no hidden clipping.
 const uniques=[],ids=[],frequency=[];
 for(const p of arrangement){const key=JSON.stringify(p);let id=uniques.findIndex(x=>JSON.stringify(x)===key);if(id<0){id=uniques.length;uniques.push(p);frequency.push(0);}frequency[id]++;ids.push(id);}
 const distance=(a,b)=>{let d=0;for(let t=0;t<3;t++)for(let s=0;s<steps;s++){const x=a[t][s],y=b[t][s];d+=(x.drums!==y.drums?4:0)+Math.abs(x.count-y.count)*4;for(let j=0;j<4;j++)d+=Math.abs(x.notes[j]-y.notes[j])/12;if(x.enabled&&y.enabled)d+=Math.abs(x.gate-y.gate)/16;}return d;};
 let chosen=[];if(uniques.length){chosen.push(frequency.indexOf(Math.max(...frequency)));while(chosen.length<Math.min(16,uniques.length)){let best=-1,score=-1;for(let j=0;j<uniques.length;j++)if(!chosen.includes(j)){const distinct=Math.min(...chosen.map(i=>distance(uniques[j],uniques[i])));const value=(distinct+0.01)*Math.sqrt(frequency[j]);if(value>score){score=value;best=j;}}chosen.push(best);}}
 if(opts._unbounded)chosen=uniques.map((_,i)=>i);
 const patterns=chosen.map(i=>uniques[i]);let reused=0;
 const order=ids.map(id=>{const exact=chosen.indexOf(id);if(exact>=0)return exact;reused++;let best=0,d=Infinity;patterns.forEach((p,j)=>{const x=distance(uniques[id],p);if(x<d){d=x;best=j;}});return best;});
 if(reused&&!opts.compress)errors.push(`${reused} bars need backing-pattern reduction. Enable reuse, omit parts, or split the song.`);
 const bends=m.tracks.filter(t=>t.id===melody.id||assignments[t.id]!=='omit'&&assignments[t.id]).reduce((s,t)=>s+t.bends,0);if(bends)report.push(`${bends} pitch-bend events are not represented.`);
 report.push(`Timing rounded to sixteenths; maximum vocal shift ${(maxRounding*tempo/4000).toFixed(0)} ms.`,`${ornaments} vocal ornaments on the same grid position omitted.`,`${legato} legato release overlaps shortened to the next onset.`,`${ties} vowel continuations; ${unmatched} unaligned notes omitted.`,`${collisions} backing note collisions reduced; ${unmapped} unsupported drum hits omitted; ${dropped} long backing gates shortened.`);if(reused)report.push(`${reused} bars reuse the nearest of 16 backing patterns.`);
 const model={format:'386dx-sketch-v1',name:ascii(opts.name||'Imported karaoke').slice(0,23),bpm,steps,bars,lines:[...lines,...Array(Math.max(0,32-lines.length)).fill('')],notes,backing:{patterns,order,patches:opts.patches||[5,3,0]},karaoke:{source:stream.id,chunkCounts:phrases.map((p,i)=>({text:p.text,count:p.chunks.length,chunks:p.chunks.map(c=>c.text),from:q(p.chunks[0].tick),to:q(phrases[i+1]?.chunks[0]?.tick??m.maxTick+1)})),alignment:'MIDI syllable chunks; hear singing to check pronunciation.'}};
 return {model,report,errors:[...new Set(errors)],phrases,counts:{bars,lines:lines.length,notes:notes.length,patterns:patterns.length}};
}
export const LEGACY_SONG_BYTES=15096;
export const V4_SONG_BYTES=15252;
export const V5_SONG_BYTES=15836;
export const SONG_BYTES=16032;
export const SONG_V5={event:15249,line:15761,section:15793,song:15801,sound:15802,sends:15818,time:15826,decay:15827,motion:15828,rate:15832};
function legacySongBytes(model){
 const a=new Uint8Array(V5_SONG_BYTES);if(model.native)a.set(model.native.slice(0,V5_SONG_BYTES));const v=new DataView(a.buffer);v.setUint32(0,0x44585331,true);v.setUint16(4,5,true);v.setUint16(6,model.bpm,true);a.fill(0,8,32);a.set([...model.name].map(c=>c.charCodeAt(0)),8);a[32]=model.bars;a[33]=model.steps;
 for(let t=0;t<4;t++){a[36+t*4]=t?model.backing?.patches?.[t-1]||0:0;if(!model.native)a[37+t*4]=t?65:95;}
 for(let p=0;p<16;p++)for(let t=0;t<3;t++)for(let s=0;s<16;s++){const n=model.backing?.patterns?.[p]?.[t]?.[s]||blank(),at=52+((p*3+t)*16+s)*12;v.setUint16(at,n.drums,true);a.set(n.notes,at+2);a[at+6]=n.count;a[at+7]=n.velocity;a[at+8]=n.gate;a[at+10]=n.enabled;}
 model.lines.forEach((line,i)=>{a.fill(0,9268+i*64,9268+(i+1)*64);a.set([...line].map(c=>c.charCodeAt(0)),9268+i*64);});
 if(model.sung!==0)a.fill(255,11476,11604);for(let b=0;b<128;b++){a[11348+b]=model.backing?.order?.[b]||0;if(model.sung!==0||!model.native)a[11732+b]=100;}
 v.setUint16(11860,model.notes.length,true);a[11862]=model.sung??1;a[11863]=model.expression??a[11863];
 model.notes.forEach((n,i)=>{const at=11864+i*6;v.setUint16(at,n.at,true);a[at+2]=n.length;a[at+3]=n.note;a[at+4]=n.line;a[at+5]=n.syllable+(n.tie?128:0);});if(!model.native)for(let l=0;l<32;l++)a.set([100,0,3,100,1],14936+l*5);
 if(model.lineVibrato)model.lineVibrato.forEach((v,i)=>{a[14936+i*5+2]=v?v-1:3;});
 a.fill(0,15096,SONG_V5.event);model.notes.forEach((n,i)=>{a[15096+(i>>2)]|=(n.vibrato===true?2:n.vibrato===false?1:0)<<((i%4)*2);});
 const sections=model.sections?.length?model.sections:[{from:0,to:model.bars,vibrato:0}];if(sections.length>8)throw Error('A song can contain up to eight sections.');sections.forEach((section,i)=>{a.set([section.from,section.to,section.vibrato||0],15224+i*3);});a[15248]=sections.length;
 const hasV5=model.native?.length>=V5_SONG_BYTES&&model.native[4]>=5;if(!hasV5){a.fill(255,SONG_V5.event,SONG_V5.song);a.fill(0,SONG_V5.song,V5_SONG_BYTES);for(let t=0;t<4;t++)a[SONG_V5.sound+t*4]=50;a[SONG_V5.time]=42;a[SONG_V5.decay]=45;a.fill(50,SONG_V5.rate,V5_SONG_BYTES);}
 const amount=v=>Number.isInteger(v)&&v>=0&&v<=100||v===255?v:(()=>{throw Error('Expression amounts must be 0–100, or inherit.');})();
 model.notes.forEach((n,i)=>{if(n.vibAmount!==undefined)a[SONG_V5.event+i]=amount(n.vibAmount);});
 if(model.lineAmounts)model.lineAmounts.forEach((v,i)=>{if(i<32)a[SONG_V5.line+i]=amount(v);});sections.forEach((n,i)=>{if(n.amount!==undefined)a[SONG_V5.section+i]=amount(n.amount);});if(model.vibAmount!==undefined){a[SONG_V5.song]=amount(model.vibAmount);if(model.vibExplicit!==false)a[35]|=1;else a[35]&=254;}
 for(const [name,offset,count] of [['sounds',SONG_V5.sound,4],['fxSends',SONG_V5.sends,2]])if(model[name])model[name].forEach((values,t)=>{if(t<4)values.forEach((v,j)=>{if(j<count)a[offset+t*count+j]=amount(v);});});
 if(model.fxTime!==undefined)a[SONG_V5.time]=amount(model.fxTime);if(model.fxDecay!==undefined)a[SONG_V5.decay]=amount(model.fxDecay);if(model.trackRate)model.trackRate.forEach((v,i)=>{if(i<4)a[SONG_V5.rate+i]=amount(v);});if(model.trackMotion)model.trackMotion.forEach((v,i)=>{if(i<4)a[SONG_V5.motion+i]=amount(v);});if(model.mutes)model.mutes.forEach((v,i)=>{if(i<4)a[38+i*4]=v?1:0;});if(model.mix)model.mix.forEach((v,i)=>{if(i<4)a[37+i*4]=amount(v);});return a;
}
/* The wire stores unique tunes and bindings; notes remains the resolved binding-ID view. */
export const SONG_V6={melody:11864,binding:13912,bindingCount:15836,passageCount:15838,arrangementCount:15839,passage:15840,arrangement:15872,names:15968,owner:16000,legacyMappingFlags:35};
/* Stale mapping records remain serializable; ownership decides whether they play. */
export function legacyCueLine(model,passage,canonicalLine){
 if(!Number.isInteger(canonicalLine)||canonicalLine<0||canonicalLine>=32)return 255;
 const p=model.passages?.[passage];if(!p)return canonicalLine;
 const mapping=model.legacyMappings?.find(m=>m.passage===passage&&m.canonicalLine===canonicalLine);
 if(mapping)return model.lineOwners?.[mapping.line]===passage?mapping.line:255;
 if(model.passages.slice(0,passage).some(prior=>prior.source===p.source))return 255;
 const owner=model.lineOwners?.[canonicalLine];return owner!==undefined&&owner!==255&&owner!==passage&&model.passages[owner]?.source===p.source?255:canonicalLine;
}
const bankEdit=Symbol();
const tuneKeys=['at','length','note','vibrato','vibAmount'];
const sameValue=(a,b)=>a===b;
export function melodyIdOf(model,id){
 const melody=model.melody||model.notes;if(id<melody.length)return id;
 for(const p of model.passages||[]){const section=model.sections?.[p.source];if(!section)continue;const first=melody.findIndex(n=>n.at>=section.from*model.steps),count=melody.filter(n=>n.at>=section.from*model.steps&&n.at<section.to*model.steps).length;if(id>=p.firstBinding&&id<p.firstBinding+count)return first+id-p.firstBinding;}return -1;
}
export function songBanks(model){
 if(!model.melody&&!model.bindings)return {melody:model.notes.map(n=>Object.fromEntries(tuneKeys.filter(k=>n[k]!==undefined).map(k=>[k,n[k]]))),bindings:model.notes.map(n=>({line:n.line,syllable:n.syllable,tie:!!n.tie})),passages:[],arrangement:[]};
 if(!Array.isArray(model.melody)||!Array.isArray(model.bindings))throw Error('Keep both the tune bank and word bindings in a reusable song.');
 const melody=model.melody.map(n=>({...n})),bindings=model.bindings.map(n=>({...n})),passages=(model.passages||[]).map(p=>({...p})),arrangement=(model.arrangement||[]).map(a=>({...a}));
 if(model.notes.length!==bindings.length)throw Error('Reusable note edits must keep the binding IDs. Add or remove notes through the passage editor.');
 const raw=model.native?.[4]===6?new Uint8Array(model.native):null,view=raw?new DataView(raw.buffer):null,changes=new Map();
 const conflict=()=>{throw Error('Conflicting edits to a shared tune. Edit the shared tune once so every passage follows, or make a word variation before changing its words.');};
 for(let id=0;id<bindings.length&&!model[bankEdit];id++){
  const mid=melodyIdOf(model,id),n=model.notes[id];if(mid<0||mid>=melody.length)throw Error('A word binding has no source tune.');
  const original=view?{at:view.getUint16(11864+mid*4,true),length:raw[11866+mid*4],note:raw[11867+mid*4],vibrato:((raw[15096+(mid>>2)]>>((mid%4)*2))&3)===2?true:((raw[15096+(mid>>2)]>>((mid%4)*2))&3)===1?false:undefined,vibAmount:raw[15249+mid]}:model.melody[mid];
  for(const key of tuneKeys){if(sameValue(n[key],original[key]))continue;const ck=mid+':'+key;if(changes.has(ck)&&!sameValue(changes.get(ck),n[key]))conflict();if(!sameValue(melody[mid][key],original[key])&&!sameValue(melody[mid][key],n[key]))conflict();changes.set(ck,n[key]);melody[mid][key]=n[key];}
  const originalBinding=raw?{line:raw[13912+id*2],syllable:raw[13913+id*2]&127,tie:!!(raw[13913+id*2]&128)}:model.bindings[id];
  for(const key of ['line','syllable','tie'])if(!sameValue(n[key],originalBinding[key])){if(!sameValue(bindings[id][key],originalBinding[key])&&!sameValue(bindings[id][key],n[key]))conflict();bindings[id][key]=n[key];}
 }
 return {melody,bindings,passages,arrangement};
}
export function resolveSongModel(model){
 const banks=songBanks(model),basis={...model,...banks};return {...basis,notes:banks.bindings.map((b,id)=>({...banks.melody[melodyIdOf(basis,id)],...b}))};
}
export function validateSongModel(model){
 const b=songBanks(model),int=(n,a,z)=>Number.isInteger(n)&&n>=a&&n<=z;
 if(!int(model.bars,1,128)||![12,16].includes(model.steps)||!int(model.bpm,40,240)||typeof model.name!=='string'||model.name.length>23||/[^\x20-\x7e]/.test(model.name)||!Array.isArray(model.lines)||model.lines.length>32||model.lines.some(line=>typeof line!=='string'||line.length>63||/[^\x20-\x7e]/.test(line)))throw Error('Invalid native song header or lyric text.');
 const mappings=model.legacyMappings??[];if(!Array.isArray(mappings)||mappings.length>63||b.bindings.length+mappings.length>512)throw Error('Legacy lyric mappings and note bindings share the 512-record bank.');
 if(b.melody.length>512||b.bindings.length<b.melody.length||b.bindings.length>512||b.passages.length>8||b.arrangement.length>32)throw Error('Tune, binding or passage capacity exceeded.');
 const sections=model.sections?.length?model.sections:[{from:0,to:model.bars,vibrato:0}];
 if(sections.length>8||sections.some((s,i)=>!int(s.from,0,127)||!int(s.to,s.from+1,model.bars)||i&&s.from<sections[i-1].to))throw Error('Source sections must be ordered without overlaps.');
 const basis={...model,...b,sections};
 if(model.lineOwners&&(!Array.isArray(model.lineOwners)||model.lineOwners.length!==32||model.lineOwners.some(owner=>owner!==255&&!int(owner,0,b.passages.length-1))))throw Error('Invalid lyric passage ownership.');
 const mappingKeys=new Set();for(const {passage,canonicalLine,line} of mappings){if(!int(passage,0,b.passages.length-1)||!int(canonicalLine,0,31)||!int(line,0,31))throw Error('Invalid legacy lyric mapping reference.');const key=passage+':'+canonicalLine;if(mappingKeys.has(key))throw Error('A legacy cue can have only one lyric mapping per passage.');mappingKeys.add(key);}
 b.melody.forEach((n,i)=>{if(!int(n.at,0,model.bars*model.steps-1)||!int(n.length,1,255)||!int(n.note,36,84)||n.at+n.length>model.bars*model.steps||i&&n.at<b.melody[i-1].at+b.melody[i-1].length)throw Error('The shared tune contains an overlap or an out-of-range note.');});
 b.passages.forEach(p=>{if(!int(p.source,0,sections.length-1)||!int(p.category??0,0,5)||!int(p.firstBinding,0,b.bindings.length)||(p.name??'').length>3)throw Error('Invalid reusable passage reference.');const s=sections[p.source],count=b.melody.filter(n=>n.at>=s.from*model.steps&&n.at<s.to*model.steps).length;if(p.firstBinding+count>b.bindings.length)throw Error('A passage exceeds its word bindings.');});
 b.bindings.forEach((binding,id)=>{const mid=melodyIdOf(basis,id),n=b.melody[mid];if(!n||(!int(binding.line,0,31)&&binding.line!==255)||!int(binding.syllable,0,63)||typeof binding.tie!=='boolean')throw Error('Invalid word binding.');if(binding.tie){const prev=b.bindings[id-1],prior=b.melody[melodyIdOf(basis,id-1)];if(!prev||!prior||n.at!==prior.at+prior.length||binding.line!==prev.line||binding.syllable!==prev.syllable)throw Error('A vowel tie must continue its preceding syllable.');}});
 if(b.arrangement.length){
  for(const n of b.melody){const sec=sections.find(sec=>n.at>=sec.from*model.steps&&n.at<sec.to*model.steps);if(!sec||n.at+n.length>sec.to*model.steps)throw Error('A source passage cuts through a shared note.');}
  let allocated=b.melody.length;const seen=new Set();b.passages.forEach((p,index)=>{const sec=sections[p.source],first=b.melody.findIndex(n=>n.at>=sec.from*model.steps),count=b.melody.filter(n=>n.at>=sec.from*model.steps&&n.at<sec.to*model.steps).length;if(!seen.has(p.source)){if(p.firstBinding!==(first<0?b.melody.length:first))throw Error('A source passage must own its canonical bindings.');seen.add(p.source);}else{if(p.firstBinding<b.melody.length)throw Error('A word variation must own separate bindings.');allocated+=count;}if(count&&b.bindings[p.firstBinding].tie)throw Error('A passage cannot begin inside a tied vowel.');for(const prior of b.passages.slice(0,index)){const ps=sections[prior.source],n=b.melody.filter(n=>n.at>=ps.from*model.steps&&n.at<ps.to*model.steps).length;if(count&&n&&p.firstBinding<prior.firstBinding+n&&prior.firstBinding<p.firstBinding+count)throw Error('Passages cannot overlap their word binding slices.');}});if(allocated!==b.bindings.length)throw Error('Every variation binding must belong to one passage.');
 }
 let ticks=0;for(const a of b.arrangement){if(!int(a.passage,0,b.passages.length-1)||!int(a.repeat,1,16)||!int(a.transpose,-48,48))throw Error('Invalid arrangement placement.');const section=sections[b.passages[a.passage].source];ticks+=(section.to-section.from)*model.steps*a.repeat;}if(ticks>65535)throw Error('The arrangement exceeds 65535 sixteenths.');return basis;
}
export function songTicks(model){if(!model.arrangement?.length)return model.bars*model.steps;return model.arrangement.reduce((n,a)=>{const s=model.sections[model.passages[a.passage].source];return n+(s.to-s.from)*model.steps*a.repeat;},0);}
export function performanceSong(model){
 if(!model.arrangement?.length)return model;
 const basis=validateSongModel(model),notes=[],order=[],barTransposes=[],sections=[];let at=0;
 for(const placement of basis.arrangement){const passage=basis.passages[placement.passage],s=model.sections[passage.source],start=s.from*model.steps,length=(s.to-s.from)*model.steps,first=basis.melody.findIndex(n=>n.at>=start),count=basis.melody.filter(n=>n.at>=start&&n.at<start+length).length;
  for(let r=0;r<placement.repeat;r++){for(let i=0;i<count;i++){const n=basis.melody[first+i],b=basis.bindings[passage.firstBinding+i];notes.push({...n,...b,bindingId:passage.firstBinding+i,passage:placement.passage,at:at+n.at-start,note:n.note+placement.transpose});}for(let bar=s.from;bar<s.to;bar++){order.push(model.backing?.order?.[bar]||0);barTransposes.push(placement.transpose);}sections.push({from:at/model.steps,to:(at+length)/model.steps,vibrato:s.vibrato||0});at+=length;}}
 return {...model,notes,bars:at/model.steps,sections,barTransposes,backing:{...model.backing,order}};
}

export function songBytes(model){
 const banks=validateSongModel(model),canonical=banks.melody.map((n,i)=>({...n,...banks.bindings[i]})),base=legacySongBytes({...model,notes:canonical}),out=new Uint8Array(SONG_BYTES);out.set(base);const v=new DataView(out.buffer);v.setUint16(4,6,true);out.fill(0,11864,14936);
 banks.melody.forEach((n,i)=>{const at=11864+i*4;v.setUint16(at,n.at,true);out[at+2]=n.length;out[at+3]=n.note;});
 banks.bindings.forEach((b,i)=>{out[13912+i*2]=b.line;out[13913+i*2]=b.syllable|(b.tie?128:0);});v.setUint16(15836,banks.bindings.length,true);out[15838]=banks.passages.length;out[15839]=banks.arrangement.length;out.fill(255,16000,16032);if(model.lineOwners)out.set(model.lineOwners,16000);
 const mappings=model.legacyMappings??[];out[35]=(out[35]&3)|(mappings.length<<2);mappings.forEach(({passage,canonicalLine,line},i)=>{const at=13912+(512-mappings.length+i)*2;out[at]=line;out[at+1]=(passage<<5)|canonicalLine;});
 banks.passages.forEach((p,i)=>{v.setUint16(15840+i*4,p.firstBinding,true);out[15842+i*4]=p.source;out[15843+i*4]=p.category||0;const name=p.name??model.passageNames?.[i]??'';out.set([...name].map(c=>c.charCodeAt(0)),15968+i*4);});
 banks.arrangement.forEach((a,i)=>{out[15872+i*3]=a.passage;out[15873+i*3]=a.repeat;v.setInt8(15874+i*3,a.transpose);});return out;
}
export function hashBytes(a){let hash=2166136261;for(const b of a)hash=Math.imul(hash^b,16777619)>>>0;return hash;}
export function midiBytes(model){
 model=performanceSong(model);
 const ppqn=192,vlq=n=>{let out=[n&127];while(n=Math.floor(n/128))out.unshift((n&127)|128);return out;},text=s=>[...s].map(c=>c.charCodeAt(0)),be=(n,k)=>Array.from({length:k},(_,i)=>(n/2**(8*(k-i-1)))&255);
 const events=[];const tempo=Math.round(60000000/model.bpm);events.push([0,[255,81,3,...be(tempo,3)]],[0,[255,88,4,model.steps===12?3:4,2,24,8]],[0,[255,3,...vlq(model.name.length),...text(model.name)]]);
 let lastLine=-1,lastSyllable=-1;for(const n of model.notes){if(!Number.isInteger(n.note)||n.note<0||n.note>127)throw Error('A transposed vocal note is outside the MIDI pitch range.');if(n.line<32&&model.lines[n.line]&&!n.tie&&(n.line!==lastLine||n.syllable<=lastSyllable)){const line='\\'+model.lines[n.line];events.push([n.at*48,[255,1,...vlq(line.length),...text(line)]]);lastLine=n.line;}lastSyllable=n.syllable;events.push([n.at*48,[144,n.note,100]],[ (n.at+n.length)*48,[128,n.note,0]]);}
 // Explicit sixteenth grid backing parts; onboard timbres map to General MIDI hints.
 const programs=[46,89,0,33,24,0],drums=[36,38,41,45,48,37,39,56,49,46,42];for(let t=0;t<2;t++)events.push([0,[192+t+1,programs[model.backing?.patches?.[t]||0]]]);
 for(let b=0;b<model.bars;b++)for(let t=0;t<3;t++)for(let s=0;s<model.steps;s++){const n=model.backing?.patterns?.[model.backing.order[b]]?.[t]?.[s];if(!n?.enabled)continue;const at=(b*model.steps+s)*48;const ns=t===2?drums.filter((_,j)=>n.drums&(1<<j)):n.notes.slice(0,n.count),ch=t===2?9:t+1;for(const original of ns){const transposed=original+(model.barTransposes?.[b]||0),note=t!==2&&transposed>=0&&transposed<128?transposed:original;events.push([at,[144+ch,note,n.velocity]],[at+48*(n.gate>100?n.gate-100:n.gate/100),[128+ch,note,0]]);}}
 // Separate vocal and backing tracks so an exported karaoke song can be
 // imported again without mistaking accompaniment for the singing melody.
 const buckets=Array.from({length:5},()=>[]);
 for(const e of events){const bytes=e[1],ch=bytes[0]&15;const track=bytes[0]===255?(bytes[1]===1||bytes[1]===5?1:0):ch===0?1:ch===1?2:ch===2?3:4;buckets[track].push(e);}
 const chunks=buckets.map((events,track)=>{if(track){const name=['','Voice','Melody','Bass','Drums'][track];events.unshift([0,[255,3,name.length,...text(name)]]);}events.sort((a,b)=>a[0]-b[0]||((a[1][0]>>4===8?0:a[1][0]===255?1:2)-(b[1][0]>>4===8?0:b[1][0]===255?1:2)));let prev=0,body=[];for(const [tick,data] of events){const at=Math.round(tick);body.push(...vlq(at-prev),...data);prev=at;}body.push(0,255,47,0);return [...text('MTrk'),...be(body.length,4),...body];});
 return new Uint8Array([...text('MThd'),0,0,0,6,0,1,0,5,...be(ppqn,2),...chunks.flat()]);
}
export async function alignPronunciation(model,scan,wordOffsets=null){
 const next=structuredClone(model),cache=new Map();const count=async text=>{if(!cache.has(text))cache.set(text,await scan(text));const n=cache.get(text);if(n<1||n>64)throw Error(`Could not pronounce “${text}”. Edit this lyric first.`);return n;};
 const maps=new Map();
 for(const entry of model.karaoke.chunkCounts){const words=[...entry.text.matchAll(/\S+/g)],separate=await Promise.all(words.map(word=>count(word[0]))),contextCount=await count(entry.text);const actualOffsets=wordOffsets&&separate.reduce((a,b)=>a+b,0)!==contextCount?await wordOffsets(entry.text):null;let pos=0;const chunks=entry.chunks.map((text,index)=>{const r={start:pos,end:pos+text.length,index};pos+=text.length;return r;}),mapping=new Map();let base=0;
  for(const word of words){const source=chunks.filter(c=>c.start<word.index+word[0].length&&c.end>word.index),n=actualOffsets?actualOffsets.filter(o=>o>=word.index&&o<word.index+word[0].length).length:separate[words.indexOf(word)];if(!n){for(const c of source)mapping.set(c.index,{base:Math.max(0,base-1),count:1,continuation:true});continue;}for(let j=0;j<source.length;j++){const from=Math.floor(j*n/source.length),to=Math.max(from+1,Math.floor((j+1)*n/source.length));const prior=mapping.get(source[j].index);mapping.set(source[j].index,{base:prior?prior.base:base+from,count:prior?base+to-prior.base:to-from,continuation:j>0&&from===Math.floor((j-1)*n/source.length)});}base+=n;}
  const actual=await count(entry.text);if(actual!==base)throw Error(`Pronunciation needs review for “${entry.text}”: ${actual} syllables in context, ${base} separately. Edit this line before loading.`);const entries=maps.get(entry.text)||[];entries.push({from:entry.from??-Infinity,to:entry.to??Infinity,mapping});maps.set(entry.text,entries);
 }
 const distributed=new Map();
 for(const [text,entries] of maps)for(const entry of entries){
  const originals=model.notes.filter(n=>model.lines[n.line]===text&&n.at>=entry.from&&n.at<entry.to);
  if(!originals.length)continue;
  const crowded=originals.some((n,i)=>{const r=entry.mapping.get(n.syllable);return !n.tie&&r&&r.count>n.length&&n.at+r.count>(originals[i+1]?.at??entry.to);});
  if(!crowded)continue;
  const total=await count(text),start=originals[0].at,end=originals.at(-1).at+originals.at(-1).length;
  if(end-start<total)throw Error(`“${text}” has too many syllables for this phrase's editing grid.`);
  const desired=Array(total).fill(null);
  for(const n of originals){const r=entry.mapping.get(n.syllable);if(!r)continue;for(let j=0;j<r.count;j++)if(desired[r.base+j]===null)desired[r.base+j]=n.at+Math.floor(n.length*j/r.count);}
  const targets=desired.map((at,i)=>at??start+Math.floor((end-start)*i/total));targets[0]=start;
  for(let i=total-2;i>=0;i--)targets[i]=Math.min(targets[i],targets[i+1]-1);
  if(targets[0]<start){targets[0]=start;for(let i=1;i<total;i++)targets[i]=Math.max(targets[i],targets[i-1]+1);}
  if(targets.at(-1)>=end)throw Error(`“${text}” needs a longer phrase, not another tool.`);
  const bounds=[...new Set([...originals.map(n=>n.at),...targets,end])].sort((a,b)=>a-b),out=[];
  for(let i=0;i<bounds.length-1;i++){const at=bounds[i],original=originals.findLast(n=>n.at<=at);if(!original)continue;const syllable=targets.findLastIndex(t=>t<=at),previous=out.at(-1);out.push({...original,at,length:bounds[i+1]-at,syllable,tie:previous?.syllable===syllable});}
  distributed.set(entry,out);
 }
 next.notes=[];let expanded=0,remapped=0,held=0,gapTicks=0;const emitted=new Set();
 for(let originalIndex=0;originalIndex<model.notes.length;originalIndex++){const original=model.notes[originalIndex],entry=maps.get(model.lines[original.line])?.find(e=>original.at>=e.from&&original.at<e.to);if(distributed.has(entry)){if(!emitted.has(entry)){next.notes.push(...distributed.get(entry));emitted.add(entry);}continue;}let range=entry?.mapping.get(original.syllable);if(!range)throw Error(`No pronunciation match for lyric fragment ${original.syllable+1} in “${model.lines[original.line]}”.`);const n={...original};if(range.base!==original.syllable)remapped++;if(range.count>1&&!n.tie)expanded++;if(n.tie){held++;}if(n.tie){range={base:range.base+range.count-1,count:1};}if(n.length<range.count){const limit=model.notes[originalIndex+1]?.at??model.bars*model.steps;if(n.at+range.count>limit)throw Error(`“${model.lines[n.line]}” needs more timing room for its syllables.`);gapTicks+=range.count-n.length;n.length=range.count;}
  for(let i=0;i<range.count;i++){const at=n.at+Math.floor(n.length*i/range.count),end=n.at+Math.floor(n.length*(i+1)/range.count);const previous=next.notes.at(-1);next.notes.push({...n,at,length:end-at,syllable:range.base+i,joinPrevious:!!range.continuation&&range.count===1,tie:!!(n.tie||range.continuation)&&range.count===1&&previous?.line===n.line&&previous?.syllable===range.base&&at===previous.at+previous.length});}
 }
 if(next.notes.length>512&&!model.karaoke?.unbounded)throw Error('Pronunciation expansion exceeds 512 notes. Split this song.');next.karaoke.alignment='Device pronunciation verified';next.karaoke.pronunciationReport=[`${expanded} original note spans divided evenly for extra syllables; their pitch and total duration stay intact.`,`${remapped} syllable positions matched to pronunciation; ${held} continuations hold the final syllable.`,`${gapTicks} sixteenths of existing silence used for multi-syllable lyric chunks; note starts and pitches stay intact.`,`${distributed.size} dense lyric phrases redistributed across their original pitch changes; their gaps become held vowels.`];return next;
}
export async function fitArticulation(model,measure,useGaps=false,autoTempo=false){
 const next=structuredClone(model),cache=new Map();let changed=0,added=0,bridged=0,sustained=0;
 for(let i=1;i<next.notes.length;i++){const n=next.notes[i],p=next.notes[i-1];if(!n.joinPrevious||n.tie)continue;const gap=n.at-p.at-p.length;if(p.line!==n.line||p.syllable!==n.syllable||gap<0)throw Error('Split lyric chunks could not be joined.');if(!useGaps)throw Error('A split lyric syllable needs its intervening gap. Enable “Use vocal gaps” and check again.');if(p.length+gap>255)throw Error('A split lyric syllable exceeds the note limit.');p.length+=gap;n.tie=true;added+=gap;bridged++;}
 for(const n of next.notes)delete n.joinPrevious;
 if(autoTempo){let ceiling=next.bpm;
  for(let i=0;i<next.notes.length;i++){const n=next.notes[i];if(n.tie)continue;let j=i;while(next.notes[j+1]?.tie)j++;if(!cache.has(next.lines[n.line]))cache.set(next.lines[n.line],await measure(next.lines[n.line]));const ms=cache.get(next.lines[n.line])[n.syllable],available=(next.notes[j+1]?.at??next.bars*next.steps)-n.at;if(Number.isFinite(ms)&&ms>0)ceiling=Math.min(ceiling,Math.floor(available*60000/(ms*4)));}
  if(ceiling<40)throw Error('A syllable still cannot fit at the slowest tempo. Give that word more time in Edit song.');
  if(ceiling<next.bpm){next.karaoke.pronunciationReport.push(`Tempo adjusted from ${next.bpm} to ${ceiling} BPM so every syllable can be sung.`);next.bpm=ceiling;}
 }
 for(let i=0;i<next.notes.length;){
  const start=i,n=next.notes[i],text=next.lines[n.line];let ticks=n.length;
  while(i+1<next.notes.length&&next.notes[i+1].tie){i++;ticks+=next.notes[i].length;}
  if(!cache.has(text))cache.set(text,await measure(text));const ms=cache.get(text)[n.syllable];
  if(!Number.isFinite(ms)||ms<1)throw Error(`Could not check articulation for “${text}”.`);
  const needed=Math.ceil(ms*next.bpm*4/60000),extra=Math.max(0,needed-ticks),last=next.notes[i];
  if(extra){const end=last.at+last.length,limit=next.notes[i+1]?.at??next.bars*next.steps;
   if(!useGaps)throw Error(`“${text}” has a syllable too short to sing. Enable “Use vocal gaps” and check again, or lengthen its note.`);
   if(end+extra>limit||last.length+extra>255){const max=Math.floor((limit-next.notes[start].at)*60000/(ms*4));throw Error(`“${text}” needs more room before its next vocal note. Try ${Math.min(next.bpm-1,max)} BPM or slower, or edit its timing.`);}
   last.length+=extra;changed++;added+=extra;
  }
  const group=next.notes.slice(start,i+1);
  if((ticks+extra)*60000/(next.bpm*4)>4500){sustained++;}
  i++;
 }
 next.karaoke.pronunciationReport.push(`${changed} short syllables extended; ${bridged} split-syllable gaps joined. ${added} extra sixteenths from existing vocal gaps; all pitches and note starts stay intact.`);
 if(sustained)next.karaoke.pronunciationReport.push(`${sustained} long held vowels use a short repeated source to fit the voice memory.`);
 next.karaoke.alignment='Pronunciation and articulation verified';return next;
}
function decodeLegacySong(input){
 const a=new Uint8Array(input);if(![V5_SONG_BYTES,V4_SONG_BYTES,LEGACY_SONG_BYTES].includes(a.length))throw Error('Wrong native song size.');const v=new DataView(a.buffer,a.byteOffset,a.byteLength);if(v.getUint32(0,true)!==0x44585331||![3,4,5].includes(v.getUint16(4,true)))throw Error('Unsupported native song format.');if(({3:LEGACY_SONG_BYTES,4:V4_SONG_BYTES,5:V5_SONG_BYTES})[v.getUint16(4,true)]!==a.length||a.length>=V4_SONG_BYTES&&a[15248]>8)throw Error('Incomplete native song format.');const str=(at,n)=>String.fromCharCode(...a.slice(at,at+n)).split('\0')[0];
 const model={format:'386dx-sketch-v1',name:str(8,24),bpm:v.getUint16(6,true),bars:a[32],steps:a[33],lines:Array.from({length:32},(_,i)=>str(9268+i*64,64)),notes:[],native:Array.from(a),legacyMappings:[],sung:a[11862],expression:a[11863],sections:a.length>=V4_SONG_BYTES?Array.from({length:a[15248]},(_,i)=>({from:a[15224+i*3],to:a[15225+i*3],vibrato:a[15226+i*3]})):[{from:0,to:a[32],vibrato:0}],backing:{patterns:[],order:Array.from(a.slice(11348,11476)),patches:[a[40],a[44],a[48]]}};
 for(let i=0;i<v.getUint16(11860,true);i++){const at=11864+i*6;if(at+6>14936)throw Error('Native score count exceeds 512.');model.notes.push({at:v.getUint16(at,true),length:a[at+2],note:a[at+3],line:a[at+4],syllable:a[at+5]&127,tie:!!(a[at+5]&128),...(a.length>=V4_SONG_BYTES&&((a[15096+(i>>2)]>>((i%4)*2))&3)?{vibrato:((a[15096+(i>>2)]>>((i%4)*2))&3)===2}:{})});}
 if(a.length===V5_SONG_BYTES){model.vibAmount=a[SONG_V5.song];model.vibExplicit=!!(a[35]&1);model.lineAmounts=Array.from(a.slice(SONG_V5.line,SONG_V5.section));model.sections.forEach((s,i)=>s.amount=a[SONG_V5.section+i]);model.notes.forEach((n,i)=>n.vibAmount=a[SONG_V5.event+i]);model.sounds=Array.from({length:4},(_,i)=>Array.from(a.slice(SONG_V5.sound+i*4,SONG_V5.sound+i*4+4)));model.fxSends=Array.from({length:4},(_,i)=>Array.from(a.slice(SONG_V5.sends+i*2,SONG_V5.sends+i*2+2)));model.fxTime=a[SONG_V5.time];model.fxDecay=a[SONG_V5.decay];model.trackMotion=Array.from(a.slice(SONG_V5.motion,SONG_V5.motion+4));model.trackRate=Array.from(a.slice(SONG_V5.rate,SONG_V5.rate+4));model.mix=Array.from({length:4},(_,i)=>a[37+i*4]);model.mutes=Array.from({length:4},(_,i)=>a[38+i*4]);}
 for(let p=0;p<16;p++)model.backing.patterns.push(Array.from({length:3},(_,t)=>Array.from({length:16},(_,s)=>{const at=52+((p*3+t)*16+s)*12;return {drums:v.getUint16(at,true),notes:Array.from(a.slice(at+2,at+6)),count:a[at+6],velocity:a[at+7],gate:a[at+8],enabled:a[at+10]};})));return model;
}

export function decodeSong(input){
 const a=new Uint8Array(input);if(a.length<8)throw Error('Wrong native song size.');const v=new DataView(a.buffer,a.byteOffset,a.byteLength),version=v.getUint16(4,true),sizes={1:11860,2:14936,3:15096,4:15252,5:15836,6:16032};
 if(v.getUint32(0,true)!==0x44585331||!sizes[version])throw Error('Unsupported native song format.');if(a.length!==sizes[version])throw Error('Incomplete native song format.');
 if(version<3){const expanded=new Uint8Array(15096);expanded.set(a);const dv=new DataView(expanded.buffer);dv.setUint16(4,3,true);if(version===1){dv.setUint16(11860,0,true);expanded[11862]=expanded[11863]=0;}for(let line=0;line<32;line++)expanded.set([100,0,3,100,1],14936+line*5);return decodeLegacySong(expanded);}
 if(version<6)return decodeLegacySong(a);
 const legacy=a.slice(0,15836),lv=new DataView(legacy.buffer),count=v.getUint16(11860,true),bindings=v.getUint16(15836,true);if(count>512||bindings<count||bindings+(a[35]>>2)>512||a[15838]>8||a[15839]>32)throw Error('Native tune or binding count exceeds capacity.');lv.setUint16(4,5,true);legacy.fill(0,11864,14936);
 for(let i=0;i<count;i++){legacy.set(a.slice(11864+i*4,11868+i*4),11864+i*6);legacy.set(a.slice(13912+i*2,13914+i*2),11868+i*6);}
 const model=decodeLegacySong(legacy);model.native=Array.from(a);model.lineOwners=Array.from(a.slice(16000,16032));model.melody=model.notes.map(n=>Object.fromEntries(tuneKeys.filter(k=>n[k]!==undefined).map(k=>[k,n[k]])));model.bindings=Array.from({length:bindings},(_,i)=>({line:a[13912+i*2],syllable:a[13913+i*2]&127,tie:!!(a[13913+i*2]&128)}));
 model.legacyMappings=Array.from({length:a[35]>>2},(_,i)=>{const at=13912+(512-(a[35]>>2)+i)*2;return {line:a[at],passage:a[at+1]>>5,canonicalLine:a[at+1]&31};});
 model.passages=Array.from({length:a[15838]},(_,i)=>({firstBinding:v.getUint16(15840+i*4,true),source:a[15842+i*4],category:a[15843+i*4],name:String.fromCharCode(...a.slice(15968+i*4,15972+i*4)).split('\0')[0]}));model.arrangement=Array.from({length:a[15839]},(_,i)=>({passage:a[15872+i*3],repeat:a[15873+i*3],transpose:v.getInt8(15874+i*3)}));
 model.notes=model.bindings.map((b,i)=>{const mid=melodyIdOf(model,i);if(mid<0||mid>=model.melody.length)throw Error('Native binding refers to a missing tune.');return {...model.melody[mid],...b};});validateSongModel(model);return model;
}
/* A structural edit changes a shared tune once, then rebuilds every owning binding slice. */
export function editTuneStructure(model,note,selectedBinding=0){
 if(!model.melody){const out=structuredClone(model);if(note){out.notes.push(note);out.notes.sort((a,b)=>a.at-b.at);}else{out.notes.splice(selectedBinding,1);if(out.notes[selectedBinding])out.notes[selectedBinding].tie=false;}return out;}
 const old=validateSongModel(model),oldCount=old.melody.length,selectedMid=melodyIdOf(old,selectedBinding),source=note?old.sections.findIndex(s=>note.at>=s.from*model.steps&&note.at<s.to*model.steps):old.sections.findIndex(s=>old.melody[selectedMid]?.at>=s.from*model.steps&&old.melody[selectedMid]?.at<s.to*model.steps);
 if(source<0)throw Error('Place the note inside a source passage. Extend that passage before adding notes beyond its end.');
 const selectedPassage=old.passages.find(p=>p.firstBinding>=oldCount&&selectedBinding>=p.firstBinding&&selectedBinding<p.firstBinding+old.melody.filter(n=>n.at>=old.sections[p.source].from*model.steps&&n.at<old.sections[p.source].to*model.steps).length),selectedBlock=selectedPassage?.firstBinding;
 const mid=note?(old.melody.findIndex(n=>n.at>=note.at)<0?oldCount:old.melody.findIndex(n=>n.at>=note.at)):selectedMid,section=old.sections[source],first=old.melody.findIndex(n=>n.at>=section.from*model.steps),local=mid-(first<0?mid:first),melody=old.melody.map(n=>({...n})),bindings=old.bindings.slice(0,oldCount).map(b=>({...b})),passages=old.passages.map(p=>({...p}));
 if(note){melody.splice(mid,0,Object.fromEntries(tuneKeys.filter(k=>note[k]!==undefined).map(k=>[k,note[k]])));bindings.splice(mid,0,selectedBlock===undefined?{line:note.line,syllable:note.syllable,tie:!!note.tie}:{line:255,syllable:0,tie:false});}else{melody.splice(mid,1);bindings.splice(mid,1);}
 const blocks=new Map();for(const p of passages){if(p.firstBinding<oldCount)continue;if(!blocks.has(p.firstBinding)){const sourceSection=old.sections[p.source],count=old.melody.filter(n=>n.at>=sourceSection.from*model.steps&&n.at<sourceSection.to*model.steps).length;blocks.set(p.firstBinding,{source:p.source,values:old.bindings.slice(p.firstBinding,p.firstBinding+count).map(b=>({...b}))});}}
 for(const [start,block] of blocks){if(block.source===source){if(note)block.values.splice(local,0,start===selectedBlock?{line:note.line,syllable:note.syllable,tie:!!note.tie}:{line:255,syllable:0,tie:false});else block.values.splice(local,1);}block.next=bindings.length;bindings.push(...block.values);}
 for(const p of passages){if(p.firstBinding>=oldCount)p.firstBinding=blocks.get(p.firstBinding).next;else{const sec=old.sections[p.source],at=melody.findIndex(n=>n.at>=sec.from*model.steps);p.firstBinding=at<0?melody.length:at;}}
 const out={...model,melody,bindings,passages,[bankEdit]:true};out.notes=bindings.map((b,id)=>({...melody[melodyIdOf(out,id)],...b}));
 for(let id=0;id<out.notes.length;id++){const n=out.notes[id],p=out.notes[id-1];if(n.tie&&(!p||n.at!==p.at+p.length||n.line!==p.line||n.syllable!==p.syllable)){n.tie=false;bindings[id].tie=false;}}
 return {...model,...decodeSong(songBytes(out))};
}

export function selectionPlayback(model,indices,event=indices[0]){
 const ns=indices.map(i=>model.notes[i]);if(!ns.length)throw Error('No notes in this selection.');
 let from=Math.min(...ns.map(n=>n.at)),to=Math.max(...ns.map(n=>n.at+n.length));
 if(!model.arrangement?.length)return {model,range:{from,to}};
 const banks=validateSongModel(model),passage=banks.passages.findIndex(p=>{const sec=banks.sections[p.source],count=banks.melody.filter(n=>n.at>=sec.from*model.steps&&n.at<sec.to*model.steps).length;return event>=p.firstBinding&&event<p.firstBinding+count;});if(passage<0)throw Error('Choose a note in a reusable passage.');
 const p=banks.passages[passage],s=banks.sections[p.source];from=Math.max(from,s.from*model.steps)-s.from*model.steps;to=Math.min(to,s.to*model.steps)-s.from*model.steps;
 return {model:{...model,arrangement:[{passage,repeat:1,transpose:0}]},range:{from,to}};
}
