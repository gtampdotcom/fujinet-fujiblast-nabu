dofile((os.getenv("FBTEST") or ".").."/keys.lua")
local f=0
TYPE(500,"TE"..string.char(13))
local J=manager.machine.ioport.ports[":kbd:nabu_hle:JOYSTICK1"].fields
local sp=manager.machine.devices[":maincpu"].spaces["program"]
local function on(t) for _,s in ipairs(t) do if f>=s and f<s+6 then return 1 end end return 0 end
local DX=tonumber(os.getenv("DX") or "7B59",16) local DY=DX+4 local ST=tonumber(os.getenv("STA") or "77D5",16)
local px,py,h,run=nil,nil,{},0
local stops=0
emu.register_frame_done(function()
 f=f+1
 J["P1 Button 1"]:set_value(on({300,900,1000}))
 RUNSCHED(f)
 if f>1500 and f<=3600 then if f%300==0 then print("ph",sp:read_u8(ST)) end
  local x,y=sp:read_u8(DX),sp:read_u8(DY)
  if px then local d=math.abs(x-px)+math.abs(y-py) h[d]=(h[d] or 0)+1 end
  px,py=x,y
 end
 if f==3600 then local k={} for a in pairs(h) do k[#k+1]=a end table.sort(k) local s="bot0 drawn step per frame:" for _,a in ipairs(k) do s=s.." "..a.."x"..h[a] end print(s) manager.machine:exit() end
end)
