-- link.lua (dofile'd by speed.lua when WATCH is set): prints each change of the
-- link's state (WATCH = "name=hex,name=hex,..." addresses from the map). With
-- RXQ=hex (the ring) and RXW=hex (its write index), the ring's last 48 bytes
-- are shown when lost or garbled turns 1.
local sp=manager.machine.devices[":maincpu"].spaces["program"]
local W={}
for n,a in string.gmatch(os.getenv("WATCH"),"(%w+)=(%x+)") do W[#W+1]={n,tonumber(a,16),nil} end
local RXQ,RXW=tonumber(os.getenv("RXQ") or "",16),tonumber(os.getenv("RXW") or "",16)
local wf=0
emu.register_frame_done(function()
 wf=wf+1
 local ch,bad={},false
 for _,w in ipairs(W) do local v=sp:read_u8(w[2]) if v~=w[3] then ch[#ch+1]=w[1].."="..v
   if (w[1]=="lost" or w[1]=="garbled") and v==1 then bad=true end w[3]=v end end
 if #ch>0 and wf>1000 then print("f"..wf..": "..table.concat(ch," ")) end
 if bad and RXQ then
  local w=sp:read_u8(RXW) local s=""
  for i=48,1,-1 do s=s..string.format("%02X ",sp:read_u8(RXQ+((w-i)&255))) end
  print("  ring before w="..w..": "..s)
 end
end)
