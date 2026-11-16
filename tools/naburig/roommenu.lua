-- roommenu.lua: the room list's REFRESH line and ESC back to the name (NABU).
-- TN = _table_n's address (hex, from the map): 0xFF while a list is asked for.
dofile((os.getenv("FBTEST") or ".").."/keys.lua")
local f=0
local J=manager.machine.ioport.ports[":kbd:nabu_hle:JOYSTICK1"].fields
local sp=manager.machine.devices[":maincpu"].spaces["program"]
local TN=tonumber(os.getenv("TN"),16)
local esc=K("Esc")
local ev={}            -- frame -> action
local function at(t,a) ev[t]=ev[t] or {} table.insert(ev[t],a) end
local stage,lasttn=0,0xFF
TYPE(1150,"NABU\r")
local hold={}
emu.register_frame_done(function()
 f=f+1
 local tn=sp:read_u8(TN)
 if f>1100 and tn<=8 and lasttn==0xFF then   -- a list has come
  stage=stage+1
  print("list "..stage.." at frame "..f.." ("..tn.." rooms)")
  if stage==1 then
   for i=0,3 do at(f+100+i*40,"down") end
   at(f+270,"shot") at(f+280,"fire")
  elseif stage==2 then
   at(f+60,"shot") at(f+120,"esc") at(f+200,"shot")
   TYPE(f+240,"X\r")
  elseif stage==3 then
   at(f+80,"shot") at(f+100,"down") at(f+150,"shot") at(f+160,"fire") at(f+400,"shot") at(f+600,"shot") at(f+900,"shot") at(f+920,"exit")
  end
 end
 lasttn=tn
 local d,b,e=0,0,0
 if f>=995 and f<1001 then b=1 end
 for t,acts in pairs(ev) do if f>=t and f<t+6 then for _,a in ipairs(acts) do
  if a=="down" then d=1 elseif a=="fire" then b=1 elseif a=="esc" then e=1 end end end end
 if ev[f] then for _,a in ipairs(ev[f]) do
  if a=="shot" or a=="fire" then manager.machine.video:snapshot() end
  if a=="exit" then manager.machine:exit() end end end
 J["P1 Down"]:set_value(d) J["P1 Button 1"]:set_value(b) esc:set_value(e)
 RUNSCHED(f)
 if f>6000 then print("timed out at stage "..stage) manager.machine:exit() end
end)
