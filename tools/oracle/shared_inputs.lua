-- Operand boundary inputs with byte-lane provenance. These are captured
-- sub-CPU writes, not independently scheduled sub execution. Main writes
-- clear provenance so replay cannot replace a computed main result.
local M={}
function M.allowed(a)
 return (a>=0x29c088 and a<0x29c09a) or a==0x29c09e or a==0x29c0a0
  or a==0x29c0a8 or a==0x29c0b4 or (a>=0x29c1ba and a<0x29c1c0)
  or (a>=0x29c620 and a<0x29c632) or (a>=0x2a3800 and a<0x2a3c00)
end
function M.new()
 local dirty={}
 return {
  write=function(a,m,sub)
   if m&0xff00~=0 then dirty[a]=sub end
   if m&255~=0 then dirty[a+1]=sub end
  end,
  read=function(list,a,d,m)
   if not M.allowed(a) then return end
   local sm=(dirty[a] and 0xff00 or 0)|(dirty[a+1] and 255 or 0)
   list[#list+1]=string.pack('>I4I4I4I4',a,d,m,sm&m)
  end
 }
end
function M.pack(list) return string.pack('>I4',#list)..table.concat(list) end
return M
