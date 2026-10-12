-- Declarative frontend. All code generation is performed by the C++ compiler.
local M, methods = {}, {}
local types = {u8=true,u16=true,u32=true,u64=true,i8=true,i16=true,i32=true,i64=true,
  f32=true,f64=true,ptr=true,size=true}
local keywords = {}
for word in ([[auto break case char const continue default do double else enum extern
  float for goto if inline int long register restrict return short signed sizeof
  static struct switch typedef union unsigned void volatile while _Bool _Complex
  _Imaginary alignas alignof bool class namespace new delete template this true false
  nullptr operator private protected public virtual]]) :gmatch("%S+") do keywords[word]=true end
local function identifier(s)
  assert(type(s)=="string" and s:match("^[A-Za-z][A-Za-z0-9_]*$") and not keywords[s],
    "VMWeave: invalid or reserved C identifier '"..tostring(s).."'")
end
local function integer(n, lo, hi)
  return type(n)=="number" and n==math.floor(n) and n>=lo and n<=hi
end
local function copy(t)
  if type(t)~="table" then return t end
  local out={}; for k,v in pairs(t) do out[k]=copy(v) end; return out
end
local function keys(t, allowed)
  assert(type(t)=="table", "VMWeave: expected a table")
  for k in pairs(t) do assert(allowed[k], "VMWeave: unknown attribute '"..tostring(k).."'") end
end
local function location(level)
  local d=debug.getinfo(level or 3,"Sl")
  if not d then return {file="<lua>",line=0} end
  return {file=d.source:gsub("^@",""), line=math.max(d.currentline,0)}
end
local function receiver(vm) assert(getmetatable(vm)==methods,"VMWeave: use colon method calls") end
methods.__index=methods
function methods:state(name, attributes)
  receiver(self); identifier(name)
  keys(attributes,{type=true,count=true}); assert(types[attributes.type],"VMWeave: unknown state type")
  assert(attributes.count==nil or integer(attributes.count,1,1048576),"VMWeave: invalid array count")
  for _,f in ipairs(self.fields) do assert(f.name~=name,"VMWeave: duplicate state '"..name.."'") end
  local f=copy(attributes); f.name=name; self.fields[#self.fields+1]=f; return self
end
function methods:instruction(name, attributes)
  receiver(self); identifier(name); attributes=attributes or {}
  keys(attributes,{opcode=true,body=true,semantics=true,operands=true,stack_effect=true,flow=true,source=true})
  assert(not(attributes.body and attributes.semantics),"VMWeave: body and semantics are aliases")
  local body=attributes.semantics or attributes.body or ""
  assert(type(body)=="string" and not body:find("\0",1,true),"VMWeave: invalid C semantics")
  assert(attributes.opcode==nil or integer(attributes.opcode,0,2147483647),"VMWeave: invalid opcode")
  for _,i in ipairs(self.instructions) do
    assert(i.name~=name,"VMWeave: duplicate instruction '"..name.."'")
    assert(attributes.opcode==nil or attributes.opcode~=i.opcode,"VMWeave: duplicate opcode")
  end
  local i=copy(attributes); i.name=name; i.semantics=body; i.body=nil
  i.source=i.source or location(); self.instructions[#self.instructions+1]=i; return self
end
local vmkeys={name=true,stack=true,instructions=true,fields=true,execution=true,components=true,
  hooks=true,memory=true,subsystems=true,rewrites=true,frame_capacity=true,ipc_capacity=true,source=true}
function M.vm(description)
  if type(description)=="string" then description={name=description,execution="none",components={"insncode"}} end
  keys(description,vmkeys); identifier(description.name)
  local vm=setmetatable(copy(description),methods)
  vm.source=vm.source or location(); vm.fields={}; vm.instructions={}
  for _,f in ipairs(description.fields or {}) do vm:state(f.name,{type=f.type,count=f.count}) end
  for _,i in ipairs(description.instructions or {}) do
    local a=copy(i); local name=a.name; a.name=nil; a.source=a.source or vm.source; vm:instruction(name,a)
  end
  M.last=vm; return vm
end
local function literal(v)
  if type(v)=="string" then return string.format("%q",v) end
  if type(v)=="number" or type(v)=="boolean" then return tostring(v) end
  assert(type(v)=="table","VMWeave: unsupported specification value")
  local ks={}; for k in pairs(v) do ks[#ks+1]=k end
  table.sort(ks,function(a,b) return tostring(a)<tostring(b) end)
  local out={"{"}; for _,k in ipairs(ks) do out[#out+1]="["..literal(k).."]="..literal(v[k]).."," end
  out[#out+1]="}"; return table.concat(out)
end
function methods:emit(options)
  receiver(self); options=options or {}; keys(options,{dispatch=true,hooks=true})
  local d=copy(self)
  if options.dispatch then
    assert(options.dispatch=="switch" or options.dispatch=="subroutine" or options.dispatch=="indirect" or options.dispatch=="direct","VMWeave: unknown dispatch")
    d.execution=options.dispatch; d.components={"insncode","dispatch"}
    local tables={subroutine="srtbl",indirect="addrtbl",direct="insntbl"}
    if tables[d.execution] then d.components[#d.components+1]=tables[d.execution] end
  end
  if options.hooks~=nil then assert(type(options.hooks)=="boolean","VMWeave: hooks must be boolean"); d.hooks=options.hooks end
  assert(not d.hooks or (d.execution and d.execution~="none"),"VMWeave: hooks require dispatch")
  if _G.__vmweave_emit then return _G.__vmweave_emit(d) end
  local input,output=os.tmpname(),os.tmpname()
  local f=assert(io.open(input,"w")); f:write("return ",literal(d)); assert(f:close())
  local cli=os.getenv("VMWEAVE_EXECUTABLE") or "vmweave-cli"
  local function quote(s) return "'"..s:gsub("'","'\\''").."'" end
  local ok=os.execute(quote(cli).." --emit-c "..quote(input).." > "..quote(output))
  os.remove(input)
  if not ok then os.remove(output); error("VMWeave: C++ generation failed") end
  f=assert(io.open(output,"r")); local result=f:read("*a"); f:close(); os.remove(output); return result
end
return M
