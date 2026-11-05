-- Standard Lua; the Aue boundary layer is expected to be transparent.
-- The same script is executed under L0 (direct binding) and L1 (contract
-- dispatch through __index) and the outputs must match byte-for-byte.

local c = counter.new(10)
counter.add(c, 5)
counter.add(c, -2)
print("value", counter.get(c))
print("field", c.value)
c.value = 42
print("field_after", c.value, counter.get(c))

local live_before = counter.live_count()
local temp = counter.new(7)
print("live_new", counter.live_count() - live_before)
temp = nil
collectgarbage("collect")
collectgarbage("collect")
print("live_gc", counter.live_count() - live_before)

print("module", counter._module)
print("hash", counter._abi_hash)

local total = 0
for i = 1, 100 do
    total = total + i
end
print("sum", total)
