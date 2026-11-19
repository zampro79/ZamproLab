local path = assert(arg[1]) .. '/samples/lua/00_welcome.lua'
local output, frames, elapsed = {}, {}, 0
print = function(s) output[#output + 1] = s end
zam = {sleep = function(ms) elapsed = elapsed + ms end}
assert(loadfile(path))()
assert(#output == 2 and output[1] == 'Welcome to ZamproLab!')
assert(output[2] == 'Your embedded Lua laboratory is ready.' and elapsed == 0)
output = {}
zam.rgb = {set = function(r, g, b)
    assert(math.type(r) == 'integer' and math.type(g) == 'integer' and math.type(b) == 'integer')
    assert(r >= 0 and r <= 4 and g >= 0 and g <= 4 and b >= 0 and b <= 4)
    frames[#frames + 1] = {r, g, b}
end}
assert(loadfile(path))()
local targets = {{4,0,0},{0,4,0},{0,0,4},{4,0,0},{0,4,0},{0,0,4},{4,4,0},{0,4,4},{4,0,4},{4,4,4}}
assert(#frames == 40 and elapsed == 4000 and #output == 2)
for i, target in ipairs(targets) do
    local frame = frames[i * 4]
    for c = 1, 3 do assert(frame[c] == target[c]) end
end
local previous = {0, 0, 0}
for _, frame in ipairs(frames) do
    for c = 1, 3 do assert(math.abs(frame[c] - previous[c]) <= 1) end
    previous = frame
end
