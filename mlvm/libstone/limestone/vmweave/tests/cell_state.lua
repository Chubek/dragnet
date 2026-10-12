local v=require("vmweave")
local vm=v.vm("CellState")
vm:state("cell",{type="u64"})
vm:instruction("inc",{semantics="cell x=vm->cell; vm->cell=x+1;"})
return vm
