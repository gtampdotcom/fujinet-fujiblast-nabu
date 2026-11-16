-- speed.lua: joins a room (ROOMDOWN: Down presses on the room list first) and walks right,
-- down, left, up from frame T0 (3300); after 1800 frames prints our step per frame (a
-- big one: the server warped us back), the frames between drawn passes and the passes
-- skipped. MX, MY, PAS, SKP, LAT: the addresses (hex) of _mv_x, _mv_y, _nabu_passes,
-- _nabu_skip and _nabu_late from r2r/nabu/fujiblast.map. Optional: SAMPF (samp.lua, a
-- PC sample file) and WATCH (link.lua). Run it in real time (THROTTLE=" ").
dofile((os.getenv("FBTEST") or ".").."/keys.lua")
if os.getenv("SAMPF") then dofile((os.getenv("FBTEST") or ".").."/samp.lua") end
if os.getenv("WATCH") then dofile((os.getenv("FBTEST") or ".").."/link.lua") end
local f=0
local J=manager.machine.ioport.ports[":kbd:nabu_hle:JOYSTICK1"].fields
local sp=manager.machine.devices[":maincpu"].spaces["program"]
local function on(t) for _,s in ipairs(t) do if f>=s and f<s+6 then return 1 end end return 0 end
TYPE(1150,"NABU\r")
local dirs={"P1 Right","P1 Down","P1 Left","P1 Up"}
local A=function(n) return tonumber(os.getenv(n),16) end
local MX,MY,PAS,SKP,LAT=A("MX"),A("MY"),A("PAS"),A("SKP"),A("LAT")
local T0=tonumber(os.getenv("T0") or "3300")
local px,py,h,run,mv,still=nil,nil,{},{},0,0
local p0,s0,l0
local ldrawn,lastdraw,gaps=nil,0,{}
emu.register_frame_done(function()
 f=f+1
 local fire=on({1000,2000,2400,2600})
 if f>T0 then
  local d=dirs[((f-T0)//60)%4+1]
  for _,n in ipairs(dirs) do J[n]:set_value(n==d and 1 or 0) end
 end
 J["P1 Button 1"]:set_value(fire)
 local rd=tonumber(os.getenv("ROOMDOWN") or "0")
 if f<=T0 then local dn=0 for i=1,rd do if f>=1700+i*40 and f<1706+i*40 then dn=1 end end J["P1 Down"]:set_value(dn) end
 RUNSCHED(f)
 if f%300==0 and f>=900 and f<=T0+300 then manager.machine.video:snapshot() end
 if f==T0+60 then p0,s0,l0=sp:read_u16(PAS),sp:read_u16(SKP),sp:read_u16(LAT) end
 if f>T0+60 then
  local dr=(sp:read_u16(PAS)-sp:read_u16(SKP))&0xFFFF
  if ldrawn and dr~=ldrawn then local g=f-lastdraw gaps[g]=(gaps[g] or 0)+1 lastdraw=f end
  if not ldrawn then lastdraw=f end
  ldrawn=dr
  local x,y=sp:read_u8(MX),sp:read_u8(MY)
  if px then local d=math.abs(x-px)+math.abs(y-py) h[d]=(h[d] or 0)+1 end
  px,py=x,y
 end
 if f==T0+1860 then
  local k={} for a in pairs(h) do k[#k+1]=a end table.sort(k)
  local s="my step per frame:" for _,a in ipairs(k) do s=s.." "..a.."x"..h[a] end print(s)
  local k2={} for a in pairs(gaps) do k2[#k2+1]=a end table.sort(k2)
  local s2="frames between drawn passes:" for _,a in ipairs(k2) do s2=s2.." "..a.."x"..gaps[a] end print(s2)
  print(string.format("1800 frames: passes %d skipped %d late %d",(sp:read_u16(PAS)-p0)&0xFFFF,(sp:read_u16(SKP)-s0)&0xFFFF,(sp:read_u16(LAT)-l0)&0xFFFF))
  manager.machine.video:snapshot()
  manager.machine:exit()
 end
end)
