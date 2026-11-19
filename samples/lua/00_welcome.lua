-- Welcome to ZamproLab
print("Welcome to ZamproLab!")
print("Your embedded Lua laboratory is ready.")

if zam.rgb then
    local colors = {
        {4, 0, 0}, {0, 4, 0}, {0, 0, 4},
        {4, 0, 0}, {0, 4, 0}, {0, 0, 4},
        {4, 4, 0}, {0, 4, 4}, {4, 0, 4}, {4, 4, 4},
    }
    local previous = {0, 0, 0}
    for _, color in ipairs(colors) do
        for step = 1, 4 do
            local function blend(from, to)
                return math.floor((from * (4 - step) + to * step) / 4 + 0.5)
            end
            zam.rgb.set(blend(previous[1], color[1]),
                        blend(previous[2], color[2]),
                        blend(previous[3], color[3]))
            zam.sleep(100)
        end
        previous = color
    end
end
