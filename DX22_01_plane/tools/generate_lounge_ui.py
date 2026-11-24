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
for name,color,mark in [('node_battle','#d6a52a',''),('node_midboss','#722f35','boss'),('cue','#f2e8cd',''),('node_event','#6352ad','event')]:
 b='<ellipse cx="65" cy="118" rx="47" ry="8" fill="#000" opacity=".25"/>'
 b+=f'<circle cx="64" cy="60" r="48" fill="{color}" stroke="#201b15" stroke-width="2"/>'
 b+='<circle cx="64" cy="60" r="48" fill="url(#ball)"/><ellipse cx="46" cy="37" rx="13" ry="9" fill="#fff" opacity=".55"/>'
 if mark == 'boss':
  b+='<path d="M39 52l12 12 13-22 13 22 12-12-6 31H45z" fill="#d8b16b" stroke="#44351e" stroke-width="2"/>'
 if mark == 'event':
  b+='<circle cx="65" cy="62" r="32" fill="#fff3da" stroke="#372b62" stroke-width="3"/>'
  b+='<path d="M54 51c0-14 26-14 26 0 0 8-13 9-13 18" fill="none" stroke="#282143" stroke-width="8" stroke-linecap="round"/><circle cx="67" cy="81" r="4.5" fill="#282143"/>'
 save(name,128,128,b)

campfire = (
 '<ellipse cx="64" cy="117" rx="48" ry="7" fill="#000" opacity=".34"/>'
 '<path d="M30 109l66-22m-64 0l66 22" fill="none" stroke="#3a2018" stroke-width="18" stroke-linecap="round"/>'
 '<path d="M30 109l66-22m-64 0l66 22" fill="none" stroke="#aa6a39" stroke-width="10" stroke-linecap="round"/>'
 '<path d="M66 12c2 23 23 28 30 48 8 22-5 42-31 42-24 0-39-16-35-35 2-9 6-16 11-22 0 11 4 17 8 20-2-18 9-34 17-53z" fill="#ff713c" stroke="#532719" stroke-width="5" stroke-linejoin="round"/>'
 '<path d="M65 51c2 14 15 18 17 28 2 9-6 17-17 17-12 0-19-8-17-17 1-5 4-9 7-13 0 7 4 8 4 8-1-9 4-16 6-23z" fill="#ffdf77"/>'
 '<path d="M68 76c2 5 7 8 7 12s-4 7-10 7c-5 0-8-3-8-7 0-4 3-7 5-10 0 4 2 6 3 7z" fill="#fff3bd"/>'
)
save('node_rest',128,128,campfire)

shop_coins = (
 '<ellipse cx="64" cy="114" rx="50" ry="8" fill="#000" opacity=".34"/>'
 '<ellipse cx="48" cy="62" rx="31" ry="42" fill="#b87925" stroke="#654016" stroke-width="5" transform="rotate(-14 48 62)"/>'
 '<ellipse cx="43" cy="60" rx="26" ry="39" fill="#ffd168" stroke="#fff0ac" stroke-width="3" transform="rotate(-14 43 60)"/>'
 '<ellipse cx="43" cy="60" rx="18" ry="29" fill="none" stroke="#af7726" stroke-width="4" transform="rotate(-14 43 60)"/>'
 '<path d="M39 49l7-4 6 26m-7 2l13-3" fill="none" stroke="#a36819" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'
)
for y in (95, 83, 71):
 shop_coins+=(
  f'<path d="M56 {y}v9a29 10 0 0 0 58 0v-9z" fill="#bd7e24" stroke="#654016" stroke-width="3"/>'
  f'<ellipse cx="85" cy="{y}" rx="29" ry="10" fill="#ffd16b" stroke="#fff0ac" stroke-width="2"/>'
  f'<ellipse cx="85" cy="{y}" rx="20" ry="5" fill="none" stroke="#af7726" stroke-width="2"/>'
 )
save('node_shop',128,128,shop_coins)
