-- a sampling profiler: the PC every ~0.7 ms of emulated time, from frame P0 to P1
local cpu=manager.machine.devices[":maincpu"]
local P0=tonumber(os.getenv("P0") or "3900") local P1=P0+1200
local fr=0
emu.register_frame_done(function() fr=fr+1 end)
local out=io.open(os.getenv("SAMPF"),"w")
local co=coroutine.wrap(function()
  while fr<P1 do
    emu.wait(0.000731)
    if fr>=P0 and fr<P1 then out:write(string.format("%04X\n",cpu.state["PC"].value)) end
  end
  out:close()
end)
emu.register_start(function() end)
co()
