-- keys.lua: dofile'd; K(name) finds a NABU HLE key field by its name ("a  A", "Go", "Esc", "Space")
local P=manager.machine.ioport.ports
local idx={}
for t,p in pairs(P) do if t:find("nabu_hle") then for n,fl in pairs(p.fields) do idx[n]=fl end end end
function K(n) return idx[n] end
function LETTER(c) c=c:lower() return idx[c.."  "..c:upper()] or idx[c] end
-- a schedule: {frame, field} pressed for 4 frames
SCHED={}
function TYPE(f0, s) for i=1,#s do local c=s:sub(i,i) local fl
  if c=="\r" then fl=idx["Go"] elseif c==" " then fl=idx["Space"] elseif c:match("%d") then for n,x in pairs(idx) do if n:sub(1,1)==c then fl=x end end else fl=LETTER(c) end
  table.insert(SCHED,{f0+(i-1)*16,fl}) end end
function RUNSCHED(f) for _,e in ipairs(SCHED) do if e[2] then e[2]:set_value((f>=e[1] and f<e[1]+7) and 1 or 0) end end end
