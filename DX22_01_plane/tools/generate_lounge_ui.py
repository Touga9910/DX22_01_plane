"""Original vector artwork for the quiet billiard lounge UI. Requires Inkscape."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]/'assets/UI/Lounge'
ROOT.mkdir(parents=True,exist_ok=True)
DEFS='''<defs><linearGradient id="wood" x2="0" y2="1"><stop stop-color="#51341f"/><stop offset="1" stop-color="#201712"/></linearGradient><radialGradient id="ball" cx=".3" cy=".25" r=".8"><stop stop-color="#ffffff" stop-opacity=".7"/><stop offset=".4" stop-color="#ffffff" stop-opacity="0"/><stop offset="1" stop-color="#000000" stop-opacity=".6"/></radialGradient><linearGradient id="paper" x2=".6" y2="1"><stop stop-color="#f4e8c8"/><stop offset="1" stop-color="#cbbb96"/></linearGradient></defs>'''
def save(name,w,h,body):
 p=ROOT/(name+'.svg');p.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{DEFS}{body}</svg>')
 subprocess.run(['inkscape',str(p),'--export-filename='+str(p.with_suffix('.png'))],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def plate(x,y,w,h):
 return f'<rect x="{x+4}" y="{y+7}" width="{w}" height="{h}" rx="10" fill="#000" opacity=".3"/><rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="url(#wood)" stroke="#b7975c" stroke-width="2"/>'
body=''
for x in [177,557,937]:
 body+=f'<rect x="{x+5}" y="242" width="330" height="287" rx="10" fill="#000" opacity=".25"/><rect x="{x}" y="235" width="330" height="287" rx="10" fill="url(#paper)" stroke="#80673e" stroke-width="3"/>'
 body+=f'<path d="M{x+180} 440h135v65h-135l-13-32z" fill="#f3e5bd" stroke="#80673e" stroke-width="2"/>'
save('reward_background',1448,1086,body)
# Objects and their separate wooden labels. All text remains dynamic in C++.
anvil='<ellipse cx="108" cy="106" rx="88" ry="13" fill="#000" opacity=".28"/><path d="M20 24h154l-15 20h-39v23l24 24v14H65V91l20-24V44H49z" fill="#444449" stroke="#151619" stroke-width="4"/><path d="M20 24l27-13h140l-13 13z" fill="#a7a5a1"/><path d="M85 45h35v22l24 24H65l20-24z" fill="#66656a"/>'
save('reward_upgrade',470,127,anvil+plate(200,18,260,94))
coins=''
for x,y in [(48,77),(109,77),(78,40)]:
 coins+=f'<ellipse cx="{x}" cy="{y+20}" rx="29" ry="12" fill="#2b1a08" opacity=".4"/><path d="M{x-28} {y}v18a28 12 0 0 0 56 0v-18" fill="#b77817" stroke="#674313" stroke-width="2"/><ellipse cx="{x}" cy="{y}" rx="28" ry="12" fill="#f1c351" stroke="#fff0af" stroke-width="2"/><ellipse cx="{x}" cy="{y}" rx="20" ry="7" fill="none" stroke="#b98221" stroke-width="2"/>'
save('reward_money',470,127,coins+plate(180,18,280,94))
body='<rect width="1672" height="941" fill="#242323"/><rect x="28" y="20" width="1616" height="901" rx="28" fill="url(#wood)" stroke="#84603e" stroke-width="4"/><rect x="48" y="40" width="1576" height="861" rx="12" fill="#07513b"/><rect x="57" y="49" width="1558" height="843" fill="#123e2f"/>'
for x in [52,836,1620]:
 for y in [44,897]: body+=f'<circle cx="{x}" cy="{y}" r="24" fill="#090c09"/>'
body+=plate(90,205,300,420)
save('map_background',1672,941,body)
for name,color,stripe,mark in [('node_battle','#d6a52a',False,''),('node_rest','#277f72',True,''),('node_shop','#bd7726',True,''),('node_midboss','#722f35',False,'boss'),('cue','#f2e8cd',False,'')]:
 b='<ellipse cx="65" cy="118" rx="47" ry="8" fill="#000" opacity=".25"/>'
 b+=f'<circle cx="64" cy="60" r="48" fill="{color}" stroke="#201b15" stroke-width="2"/>'
 if stripe: b+='<path d="M21 39a48 48 0 0 1 86 0zM21 81a48 48 0 0 0 86 0z" fill="#e9e4d2"/>'
 b+='<circle cx="64" cy="60" r="48" fill="url(#ball)"/><ellipse cx="46" cy="37" rx="13" ry="9" fill="#fff" opacity=".55"/>'
 if mark:b+='<path d="M39 52l12 12 13-22 13 22 12-12-6 31H45z" fill="#d8b16b" stroke="#44351e" stroke-width="2"/>'
 save(name,128,128,b)
