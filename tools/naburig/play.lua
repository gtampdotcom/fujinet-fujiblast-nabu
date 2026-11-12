dofile((os.getenv("FBTEST") or ".").."/keys.lua")
local f=0
local J=manager.machine.ioport.ports[":kbd:nabu_hle:JOYSTICK1"].fields
local function on(t) for _,s in ipairs(t) do if f>=s and f<s+6 then return 1 end end return 0 end
TYPE(1150,"NABU\r")
local dirs={"P1 Right","P1 Down","P1 Left","P1 Up"}
emu.register_frame_done(function()
 f=f+1
 local fire=on({1000,2000})
 if f>2600 then
  local d=dirs[(f//45)%4+1]
  for _,n in ipairs(dirs) do J[n]:set_value(n==d and 1 or 0) end
  if f%90==20 then fire=1 end
 end
 J["P1 Button 1"]:set_value(fire)
 RUNSCHED(f)
 if f%300==0 and f>=1500 then manager.machine.video:snapshot() end
 if f==4200 then manager.machine:exit() end
end)
