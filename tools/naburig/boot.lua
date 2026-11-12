dofile((os.getenv("FBTEST") or ".").."/keys.lua")
local f=0
local J=manager.machine.ioport.ports[":kbd:nabu_hle:JOYSTICK1"].fields
local function on(t) for _,s in ipairs(t) do if f>=s and f<s+6 then return 1 end end return 0 end
TYPE(1150,"NABU\r")
emu.register_frame_done(function()
 f=f+1
 J["P1 Button 1"]:set_value(on({1000}))
 RUNSCHED(f)
 if f==990 or f==1140 or f==1300 or f==1600 or f==2200 or f==2800 or f==3600 then manager.machine.video:snapshot() end
 if f==3600 then manager.machine:exit() end
end)
