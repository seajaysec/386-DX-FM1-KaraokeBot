/* SPDX-License-Identifier: GPL-3.0-only */
import {planMidi} from './karaoke.mjs';
import {usage, LINES, ROWS, NOTES} from './song7.mjs';

// Convert MIDI clocks in the importer, never ask the player to edit another file.
export function prepareTiming(input,options={}){
 const m=structuredClone(input),report=[],events=[{tick:0,tempo:500000},...m.tempos].sort((a,b)=>a.tick-b.tick);
 const base=m.tempos[0]?.tempo||500000;let tick=0,seconds=0,tempo=500000;
 const segments=[];for(const e of events){seconds+=(e.tick-tick)*tempo/(m.ppqn*1e6);tick=e.tick;tempo=e.tempo;segments.push({tick,seconds,tempo});}
 const varying=m.tempos.some(t=>t.tempo!==base),keep=options.timing!=='even';
 const map=t=>{if(!varying||!keep)return t;let s=segments[0];for(const e of segments){if(e.tick>t)break;s=e;}return (s.seconds+(t-s.tick)*s.tempo/(m.ppqn*1e6))*m.ppqn*1e6/base;};
 const seen=new WeakSet();
 for(const t of m.tracks){for(const n of t.notes){n.tick=map(n.tick);n.end=map(n.end);}for(const e of t.texts){if(!seen.has(e)){seen.add(e);e.tick=map(e.tick);}}}
 for(const s of m.streams)for(const e of s.events){if(!seen.has(e)){seen.add(e);e.tick=map(e.tick);}}
 const musicalEnd=m.tracks.reduce((end,t)=>t.notes.reduce((end,n)=>Math.max(end,n.end),end),1);
 m.maxTick=musicalEnd-1;m.tempos=[{tick:0,tempo:base}];
 if(varying)report.push(keep?'Tempo changes kept in the note spacing; the device uses one playback clock.':'Tempo changes replaced with an even pulse.');
 const meter=m.meters[0]||{numerator:4,denominator:4};
 if(![3,4].includes(meter.numerator)||meter.denominator!==4||m.meters.some(t=>t.numerator!==meter.numerator||t.denominator!==meter.denominator)){
  m.meters=[{tick:0,numerator:4,denominator:4}];report.push('Meter changes mapped onto a four-beat editing grid; note and lyric times stay aligned.');
 }
 return {m,report};
}
export function melodySections(model){
 const spans=[];let active=null;
 for(const n of model.notes){if(!active||n.line!==active.line||(!n.tie&&n.syllable<=active.last)){active={line:n.line,from:n.at,to:n.at+n.length,last:n.syllable,notes:[]};spans.push(active);}active.notes.push(n);active.to=Math.max(active.to,n.at+n.length);active.last=n.syllable;}
 const identities=new Map();const result=spans.map(s=>{const key=s.notes.map(n=>[n.at-s.from,n.length,n.note,!!n.tie].join(',')).join(';');if(!identities.has(key))identities.set(key,identities.size);const id=identities.get(key),same=spans.filter(x=>x.notes.length===s.notes.length&&x.notes.every((n,i)=>n.at-x.from===s.notes[i].at-s.from&&n.length===s.notes[i].length&&n.note===s.notes[i].note&&!!n.tie===!!s.notes[i].tie)).length;return {line:s.line,from:s.from,to:s.to,id,label:id<26?String.fromCharCode(65+id):`M${id+1}`,repeats:same,text:model.lines[s.line]};});
 for(let i=0;i<result.length;i++){const a=spans[i];for(let j=0;j<i;j++){const b=spans[j];if(a.notes.length!==b.notes.length||a.notes.length<3||result[i].id===result[j].id)continue;const score=a.notes.reduce((cost,n,k)=>cost+Math.abs(n.note-b.notes[k].note)*2+Math.abs((n.at-a.from)-(b.notes[k].at-b.from))+Math.abs(n.length-b.notes[k].length),0)/a.notes.length;if(score<=2){result[i].suggestion=result[j].label;result[i].source=result[j].line;break;}}}return result;
}
// Preserve every note and bar. Part boundaries never bisect a vocal group.
export function songParts(full){
 const parts=[];let from=0;const end=full.bars*full.steps,sections=melodySections(full);
 while(from<end){let to=Math.min(end,from+128*full.steps);
  const inRange=cut=>full.notes.filter(n=>n.at>=from&&n.at<cut);
  const fits=cut=>{const ns=inRange(cut),u=usage(ns,full.lines);return ns.length<=NOTES&&u.lines<=LINES&&u.rows<=ROWS&&!full.notes.some(n=>n.at<cut&&n.at+n.length>cut||n.at===cut&&n.tie);};
  while(to>from&&!fits(to)){const earlier=sections.filter(s=>s.from>from&&s.from<to).at(-1)?.from;const group=full.notes.filter(n=>!n.tie&&n.at>from&&n.at<to).at(-1)?.at;to=earlier??group??to-full.steps;}
  if(to<=from)throw Error('One vocal phrase is too large for a song part. Split that lyric into shorter lines in the review.');
  // Prefer a phrase boundary over a hardware-limit cut, unless it discards an entire instrumental span.
  if(to<end){const candidate=sections.filter(s=>s.from>from&&s.from<=to&&s.from>=to-16*full.steps).at(-1)?.from;if(candidate&&fits(candidate))to=candidate;}
  const ns=inRange(to),ids=[...new Set(ns.map(n=>n.line))],map=new Map(ids.map((id,i)=>[id,i]));
  const firstBar=Math.floor(from/full.steps),lastBar=Math.ceil(to/full.steps);
  // Non-bar cuts need their backing steps rebased, not the previous bar replayed.
  const bars=Math.ceil((to-from)/full.steps),patterns=[],order=[];
  for(let b=0;b<bars;b++){
   const pattern=Array.from({length:3},(_,t)=>Array.from({length:16},(_,s)=>{const at=from+b*full.steps+s;if(s>=full.steps||at>=to)return {notes:[0,0,0,0],count:0,velocity:100,gate:100,drums:0,enabled:0};return structuredClone(full.backing.patterns[full.backing.order[Math.floor(at/full.steps)]]?.[t]?.[at%full.steps]);}));
   const key=JSON.stringify(pattern);let id=patterns.findIndex(p=>JSON.stringify(p)===key);if(id<0){id=patterns.length;patterns.push(pattern);}order.push(id);
  }
  // Rebased bar boundaries can introduce extra patterns. Reuse nearest patterns explicitly, never silently clip order.
  const backing=reducePatterns(patterns,order,full.steps);
  const part={...full,sections:undefined,native:undefined,bars,lines:ids.map(id=>full.lines[id]),notes:ns.map(n=>({...n,at:n.at-from,line:map.get(n.line)})),backing:{...full.backing,...backing},karaoke:{...full.karaoke,unbounded:false,sourceFrom:from,sourceTo:to,chunkCounts:full.karaoke.chunkCounts.filter(c=>ids.some(id=>full.lines[id]===c.text)&&(c.from===undefined||c.from<to&&c.to>from)).map(c=>({...c,...(c.from!==undefined?{from:c.from-from,to:c.to-from}:{})}))}};
const boundaries=[0,...sections.filter(s=>s.from>from&&s.from<to).map(s=>Math.floor((s.from-from)/full.steps)),bars];const cuts=[...new Set(boundaries)].sort((a,b)=>a-b);while(cuts.length>9)cuts.splice(cuts.length-2,1);part.sections=cuts.slice(0,-1).map((start,i)=>({from:start,to:cuts[i+1],vibrato:0}));parts.push(part);from=to;
 }
 if(parts.length>1)parts.forEach((p,i)=>p.name=`${full.name.slice(0,18)} ${i+1}/${parts.length}`.slice(0,23));
 return parts;
}
function reducePatterns(patterns,order,steps){
 if(patterns.length<=16)return {patterns,order,reused:0};
 const frequencies=patterns.map((_,i)=>order.filter(id=>id===i).length),chosen=[frequencies.indexOf(Math.max(...frequencies))];
 const distance=(a,b)=>{let d=0;for(let t=0;t<3;t++)for(let s=0;s<steps;s++){const x=a[t][s],y=b[t][s];d+=Math.abs(x.count-y.count)*12+(x.drums!==y.drums?12:0);for(let j=0;j<4;j++)d+=Math.abs(x.notes[j]-y.notes[j]);}return d;};
 while(chosen.length<16){let best=-1,score=-1;for(let i=0;i<patterns.length;i++)if(!chosen.includes(i)){const value=(Math.min(...chosen.map(c=>distance(patterns[i],patterns[c])))+.01)*Math.sqrt(frequencies[i]);if(value>score){score=value;best=i;}}chosen.push(best);}
 return {reused:order.filter(i=>!chosen.includes(i)).length,patterns:chosen.map(i=>patterns[i]),order:order.map(id=>chosen.includes(id)?chosen.indexOf(id):chosen.reduce((best,c,i)=>distance(patterns[id],patterns[c])<distance(patterns[id],patterns[chosen[best]])?i:best,0))};
}
export function planKaraoke(input,options={}){
 const {m,report}=prepareTiming(input,options),plan=planMidi(m,{...options,compress:options.compress!==false,_unbounded:true});
 const full=plan.model;full.karaoke.unbounded=true;
 let parts=[];try{parts=songParts(full);}catch(e){plan.errors.push(e.message);}
 const sections=melodySections(full),repeated=sections.filter(s=>s.repeats>1);
 report.push(...plan.report);const reused=parts.reduce((n,p)=>n+(p.backing.reused||0),0);if(reused)report.unshift(`${reused} backing bars use close matching patterns. Singing notes and lyrics are unchanged; hear the result before loading.`);if(parts.length>1)report.unshift(`The complete song is kept in ${parts.length} ordered parts. Load and SAVE each part separately on the FM-1; export the song set to keep them together.`);
 if(repeated.length)report.push(`${repeated.length} vocal phrases share exact melodies. Their lyrics remain independent.`);
 return {...plan,full,parts,model:parts[0]||full,sections,report,counts:{...plan.counts,parts:parts.length}};
}
export function copyMelody(model,target,source){
 const next=structuredClone(model),sections=melodySections(model),src=sections.find(s=>s.line===source),targets=sections.filter(s=>s.line===target);
 if(!src||!targets.length||source===target)throw Error('Choose two different scored lyric lines.');
 const original=model.notes.filter(n=>n.at>=src.from&&n.at<src.to);
 for(const t of targets){const ns=next.notes.filter(n=>n.at>=t.from&&n.at<t.to),nextStart=sections.find(s=>s.from>t.from)?.from??model.bars*model.steps;if(ns.length!==original.length||ns.some((n,i)=>n.syllable!==original[i].syllable||!!n.tie!==!!original[i].tie))throw Error('These lines need different syllable patterns. Edit their notes individually.');if(t.from+src.to-src.from>nextStart)throw Error('That melody is longer than this line’s available space.');ns.forEach((n,i)=>{n.at=t.from+original[i].at-src.from;n.length=original[i].length;n.note=original[i].note;});}
 return next;
}
// Trim one occurrence, closing the timeline on a bar boundary. Shared lines stay independent.
export function trimSection(model,from,to){
 const end=model.bars*model.steps;if(!Number.isInteger(from)||!Number.isInteger(to)||from<0||from>=to||to>end)throw Error('Choose a valid section to remove.');
 // Use whole bars so backing and vocal attacks retain their relative positions.
 const begin=Math.floor(from/model.steps)*model.steps,finish=Math.min(end,Math.ceil(to/model.steps)*model.steps);
 if(begin===0&&finish===end)throw Error('Keep at least one section in the song.');if(model.notes.some(n=>n.at<begin&&n.at+n.length>begin||n.at<finish&&n.at+n.length>finish)||model.notes.some(n=>n.at===finish&&n.tie))throw Error('This cut crosses a held syllable. Choose boundaries between syllables.');
 const next=structuredClone(model),length=finish-begin;next.notes=next.notes.filter(n=>n.at<begin||n.at>=finish).map(n=>({...n,at:n.at>=finish?n.at-length:n.at}));next.bars=(end-length)/model.steps;next.backing.order.splice(begin/model.steps,length/model.steps);next.native=undefined;next.sections=undefined;return {model:next,from:begin,to:finish};
}
