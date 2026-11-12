dofile((os.getenv("FBTEST") or ".").."/keys.lua")
local f=0
local J=manager.machine.ioport.ports[":kbd:nabu_hle:JOYSTICK1"].fields
local sp=manager.machine.devices[":maincpu"].spaces["program"]
local function on(t) for _,s in ipairs(t) do if f>=s and f<s+6 then return 1 end end return 0 end
emu.register_frame_done(function()
 f=f+1
 J["P1 Button 1"]:set_value(on({1000,1300,2100}))
 if f==1250 then manager.machine.video:snapshot() end
 if f==2600 then manager.machine.video:snapshot() manager.machine:exit() end
end)
