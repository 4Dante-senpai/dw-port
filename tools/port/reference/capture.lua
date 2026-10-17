-- PCSX-Redux adapter. Loaded after the generated DW1_CAPTURE table.
local c = assert(DW1_CAPTURE)
local ticks, origin, mark, nextInput = 0, nil, nil, 1
local held = {}
local breakpoints = {} -- Retain Lua breakpoint wrappers until shutdown.
local listener
local function progress(point)
    local file = assert(io.open(c.progress, 'w'))
    file:write(point .. ' at VBlank ' .. ticks .. '\n')
    file:close()
    print('reference: ' .. point .. ' at VBlank ' .. ticks)
end
local function guarded(fn)
    return function(...)
        local ok, message = pcall(fn, ...)
        if not ok then
            progress('Lua error: ' .. tostring(message))
            PCSX.quit(1)
        end
        return message
    end
end
local function input(k)
    while c.inputs[nextInput] and c.inputs[nextInput].vblank <= k do
        held = c.inputs[nextInput].buttons
        nextInput = nextInput + 1
    end
    -- Reapply the held state at every latch, including after a guest pad reset.
    local pad = PCSX.SIO0.slots[1].pads[1]
    for button = 0, 15 do pad.clearOverride(button) end
    for _, button in ipairs(held) do pad.setOverride(button) end
end
progress('before frame 0')
-- AD-15 frame 0: return of the game's first VSync call (nothing calls VSync before main).
breakpoints.vsync = PCSX.addBreakpoint(c.vsync, 'Exec', 4, 'DW1 first VSync', guarded(function()
    local ra = tonumber(PCSX.getRegisters().GPR.n.ra)
    if not origin and not breakpoints.returned then
        breakpoints.returned = PCSX.addBreakpoint(ra, 'Exec', 4, 'DW1 frame 0', guarded(function()
            if not origin then
                origin = ticks
                progress('frame 0')
                input(0)
                breakpoints.vsync:disable()
                breakpoints.returned:disable()
            end
            return true
        end))
    end
    return true
end))
breakpoints.mark = PCSX.addBreakpoint(c.mark, 'Exec', 4, 'DW1 scene mark', guarded(function()
    if not mark then
        assert(origin, 'scene mark reached before frame 0')
        mark = ticks
        progress('mark')
        breakpoints.mark:disable()
    end
    return true
end))
listener = PCSX.Events.createEventListener('GPU::Vsync', guarded(function()
    ticks = ticks + 1
    if origin then input(ticks - origin) end
    if mark and ticks - mark >= c.capture_vblank then
        -- SoftGPU returns the GP1 display rectangle, raw RGB555 or RGB24.
        local shot = PCSX.GPU.takeScreenShot()
        assert(shot.bpp == 0 or shot.bpp == 1, 'unexpected screenshot bpp')
        local file = assert(io.open(c.raw, 'wb'))
        assert(file:write(tostring(shot.data)))
        assert(file:close())
        file = assert(io.open(c.meta, 'w'))
        assert(file:write(string.format('{"width":%d,"height":%d,"bpp":%d}',
                     shot.width, shot.height, shot.bpp == 0 and 16 or 24)))
        assert(file:close())
        progress('captured')
        PCSX.quit(0)
    elseif ticks >= c.max_vblanks then
        progress(mark and 'timeout after mark' or (origin and 'timeout after frame 0' or 'timeout before frame 0'))
        PCSX.quit(1)
    end
end))
-- Keep handles reachable even if the host collects the script's locals.
DW1_REFERENCE_HANDLES = {breakpoints, listener}
