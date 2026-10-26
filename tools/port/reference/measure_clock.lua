-- Disc-free NTSC 320x240 probe. Samples the same GPU::Vsync event used by capture.lua.
local ffi = require('ffi')
local ram = ffi.cast('uint32_t*', PCSX.getMemPtr())
-- GP1(08h)=NTSC, non-interlaced, 320 px; then an uncached-free CPU loop.
local words = {0x3c081f80, 0x35081814, 0x3c090800, 0x35290001, 0xad090000,
               0x08004005, 0}
for i, word in ipairs(words) do ram[0x10000/4+i-1] = word end
local count, first, previous = 0, nil, nil
DW1_CLOCK_PROBE = PCSX.Events.createEventListener('GPU::Vsync', function()
    local cycle = tonumber(PCSX.getCPUCycles())
    count = count + 1
    if count == 2 then first = cycle end -- Exclude the initial partial video field.
    if count > 2 then
        print(string.format('clock-probe %d %.0f %.0f', count-2, cycle, cycle-previous))
    end
    previous = cycle
    if count == 258 then
        print(string.format('clock-probe-total 256 %.0f', cycle-first))
        PCSX.quit(0)
    end
end)
PCSX.getRegisters().pc = 0x80010000
