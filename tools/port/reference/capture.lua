-- PCSX-Redux adapter. Loaded after the generated DW1_CAPTURE table.
local c = assert(DW1_CAPTURE)
local ffi = require("ffi")
local bit = require("bit")
local state = c.state_trace and assert(io.open(c.state_trace, "w"))
local randIndex = 0
local ticks, origin, mark, nextInput = 0, nil, nil, 1
local held = {}
local breakpoints = {} -- Retain Lua breakpoint wrappers until shutdown.
local listener, renewListener
local listenerEpoch = 0
local checkpoints = c.checkpoints or {}
local checkpointIndex = 1
local snapshot, enterCheckpoints
local audio, audioIndex = nil, 1
local audioFinishedTick
local movie, movieFrames, movieOrigin
local timing = c.timing_trace and assert(io.open(c.timing_trace, 'w'))
local costNames = {'cd','gpu','mdec','card','psyq','wait','cpu','spu'}
local costs, costBase, costStack = {}, {}, {}
for _, name in ipairs(costNames) do costs[name], costBase[name] = 0, 0 end
local hardwarePoint, shopFinish
local costCycle, originCycle
local function accountCycles()
    local cycle = tonumber(PCSX.getCPUCycles())
    local source = #costStack > 0 and costStack[#costStack].source or 'cpu'
    if costCycle and cycle >= costCycle then
        local delta=cycle-costCycle
        costs[source] = costs[source] + delta
        if #costStack>0 then costStack[#costStack].own=costStack[#costStack].own+delta end
    end
    costCycle = cycle
    return cycle
end
local function timingPoint(event, label)
    if not timing or not origin then return end
    local cycle = accountCycles()
    if hardwarePoint then hardwarePoint(event,label) end
    local fields = {}
    for _, name in ipairs(costNames) do
        fields[#fields+1] = string.format('"%s":%.0f', name, costs[name]-costBase[name])
    end
    assert(timing:write(string.format('{"event":"%s","label":"%s","vblank":%d,"cycle":%.0f,"costs":{%s}}\n',
        event,label,ticks-origin,cycle-originCycle,table.concat(fields,','))))
    assert(timing:flush())
end
if timing then
    for _, entry in ipairs(c.timing_functions or {}) do
        breakpoints['cost_' .. entry.address] = PCSX.addBreakpoint(entry.address,'Exec',4,'DW1 cost',function()
            accountCycles()
            local ra = tonumber(PCSX.getRegisters().GPR.n.ra)
            local source=entry.source
            -- VSync(-1) is a counter query inside CD polling loops, not a wait.
            if entry.name=='VSync' and bit.tobit(PCSX.getRegisters().GPR.n.a0)<0 then source='psyq' end
            costStack[#costStack+1] = {source=source, ra=ra, name=entry.name, start=costCycle, own=0,
                a0=tonumber(PCSX.getRegisters().GPR.n.a0)}
            local key = 'cost_return_' .. ra
            if not breakpoints[key] then
                breakpoints[key] = PCSX.addBreakpoint(ra,'Exec',4,'DW1 cost return',function()
                    -- Tail calls share their caller's return address. Close every
                    -- such scope, or all later CPU time leaks into the outer API.
                    while #costStack > 0 and costStack[#costStack].ra == ra do
                        accountCycles()
                        local span=costStack[#costStack]
                        if hardwarePoint and (costCycle-span.start>=4096 or span.name=='PadRead' or span.name=='newGameStateMachine') then
                            hardwarePoint('function_span',span.name or span.source,
                                string.format(',"start":%.0f,"elapsed":%.0f,"own":%.0f,"a0":%.0f,"result":%.0f',
                                    span.start,costCycle-span.start,span.own,span.a0,tonumber(PCSX.getRegisters().GPR.n.v0)))
                        end
                        table.remove(costStack)
                    end
                    return true
                end)
            end
            return true
        end)
    end
end
local function movieStart()
    if not c.movie then return end
    movieFrames, movieOrigin = 0, ticks
    movie = assert(io.open(c.movie .. '/movie.jsonl','w'))
    assert(movie:write(string.format('{"event":"start","cycle":%.0f}\n',tonumber(PCSX.getCPUCycles()))))
    breakpoints.movieReturn=PCSX.addBreakpoint(tonumber(PCSX.getRegisters().GPR.n.ra),'Exec',4,'DW1 movie finish',function()
        assert(movie:write(string.format('{"event":"end","cycle":%.0f,"vblank":%d,"return":%d}\n',
            tonumber(PCSX.getCPUCycles()),ticks-movieOrigin,tonumber(PCSX.getRegisters().GPR.n.v0))))
        assert(movie:close()); movie=nil
        breakpoints.movieReturn:disable()
        return true
    end)
end
if c.movie then
    breakpoints.movieDisplay=PCSX.addBreakpoint(c.movie_display,'Exec',4,'DW1 movie display',function()
        if not movie then return true end
        if breakpoints.movieDisplayReturn then breakpoints.movieDisplayReturn:disable() end
        breakpoints.movieDisplayReturn=PCSX.addBreakpoint(tonumber(PCSX.getRegisters().GPR.n.ra),'Exec',4,'DW1 movie pixels',function()
            if movie then
                movieFrames=movieFrames+1
                local shot=PCSX.GPU.takeScreenShot()
                local stem=string.format('%s/frame-%04d',c.movie,movieFrames)
                local raw=assert(io.open(stem..'.raw','wb')); assert(raw:write(tostring(shot.data))); assert(raw:close())
                local meta=assert(io.open(stem..'.json','w'))
                assert(meta:write(string.format('{"width":%d,"height":%d,"bpp":%d}',shot.width,shot.height,shot.bpp==0 and 16 or 24)))
                assert(meta:close())
                assert(movie:write(string.format('{"event":"frame","index":%d,"cycle":%.0f,"vblank":%d}\n',
                    movieFrames,tonumber(PCSX.getCPUCycles()),ticks-movieOrigin)))
            end
            breakpoints.movieDisplayReturn:disable()
            return true
        end)
        return true
    end)
end
local function audioEnd(reason)
    if not audio then return end
    assert(not audio.waitNote, 'missing new note-on audio anchor')
    assert(audio.file:write(string.format('# end %.0f %s\n',
        math.floor((tonumber(PCSX.getCPUCycles())-audio.cycle)/768), reason)))
    assert(audio.file:close())
    local meta = assert(io.open(audio.meta, 'w'))
    assert(meta:write(string.format('{"start_cycle":%.0f,"end_cycle":%.0f,"vblanks":%d}',
        audio.cycle, tonumber(PCSX.getCPUCycles()), ticks-audio.tick)))
    assert(meta:close())
    audio = nil
    audioIndex = audioIndex + 1
    if audioIndex > #c.audio then audioFinishedTick = ticks end
end
local function audioCheckpoint(label)
    if not c.audio then return end
    if audio and audio.finish == label then audioEnd("checkpoint") end
    local segment = c.audio[audioIndex]
    if segment and segment.start == label then
        assert(not audio, 'overlapping audio segments')
        audio = segment
        audio.tick = ticks
        audio.cycle = tonumber(PCSX.getCPUCycles())
        audio.waitNote = audio.anchor == 'note_on'
        audio.pending, audio.first, audio.addresses = {}, {}, 0
        if audio.finish == audio.start then
            movieStart()
            local ra=tonumber(PCSX.getRegisters().GPR.n.ra)
            breakpoints.audioReturn=PCSX.addBreakpoint(ra, 'Exec', 4, 'DW1 movie return', function()
                audioEnd('return')
                breakpoints.audioReturn:disable()
                return true
            end)
        end
        audio.file = assert(io.open(audio.trace, 'w'))
        assert(audio.file:write('# dw1-spu-trace-v1\n'))
    end
end
local function audioWrite(off, value)
    local row = {cycle=tonumber(PCSX.getCPUCycles()), tick=ticks, off=off, value=value}
    local function emit(r)
        assert(audio.file:write(string.format('%d %.0f %03x %04x\n',
            r.tick-audio.tick, math.floor((r.cycle-audio.cycle)/768), r.off, r.value)))
    end
    if not audio.waitNote then emit(row); return end
    -- Identical to spu.c: one flush ends at KON hi. Only an address written
    -- for a keyed voice confirms a new note; retain its whole earlier bundle.
    if off < 0x180 or #audio.pending > 0 then
        assert(#audio.pending < 256, 'note-on anchor buffer overflow')
        table.insert(audio.pending, row)
        if off < 0x180 then
            local voice = math.floor(off/16)
            audio.first[voice] = audio.first[voice] or #audio.pending
            if off%16 == 6 then audio.addresses = bit.bor(audio.addresses, bit.lshift(1,voice)) end
        end
    end
    local keyed = off == 0x188 and value or off == 0x18a and bit.lshift(value,16) or 0
    keyed = bit.band(keyed, audio.addresses, 0xffffff)
    if keyed ~= 0 then
        local first = #audio.pending
        for voice=0,23 do
            if bit.band(keyed,bit.lshift(1,voice)) ~= 0 then first=math.min(first,audio.first[voice]) end
        end
        audio.cycle, audio.tick = audio.pending[first].cycle, audio.pending[first].tick
        audio.waitNote = false
        for i=first,#audio.pending do emit(audio.pending[i]) end
    elseif off == 0x18a then
        audio.pending, audio.first, audio.addresses = {}, {}, 0
    end
end
local function complete()
    if #checkpoints == 0 or checkpointIndex <= #checkpoints then return false end
    for _, cp in ipairs(checkpoints) do
        if ticks - cp.start < cp.window then return false end
    end
    return true
end
local function progress(point)
    local file = assert(io.open(c.progress, 'w'))
    file:write(point .. ' at VBlank ' .. ticks .. '\n')
    file:close()
    print('reference: ' .. point .. ' at VBlank ' .. ticks)
end
local function guarded(fn)
    return function(...)
        local ok, message = xpcall(fn, debug.traceback, ...)
        if not ok then
            progress('Lua error: ' .. tostring(message))
            PCSX.quit(1)
        end
        return message
    end
end
if c.hardware_trace then
    hardwarePoint = dofile(c.hardware_adapter)(c.hardware_trace, breakpoints, guarded, function()
        return #costStack > 0 and costStack[#costStack].source or 'cpu'
    end, c.hardware_map_load, c.hardware_irq)
end
if c.shop_probe then
    shopFinish = dofile(c.shop_probe.adapter)(c.shop_probe, breakpoints, guarded, function()
        assert(origin and originCycle, 'SHOP probe before frame 0')
        return ticks-origin, tonumber(PCSX.getCPUCycles())-originCycle
    end)
end
if c.audio then
    -- The write breakpoint fires before the store. The pinned core can expose the
    -- current instruction at rawPC or rawPC-4; match its effective address, never
    -- guess the value from the destination register (KON/KOFF are write-only).
    breakpoints.spu = PCSX.addBreakpoint(0x1f801c00, 'Write', 0x200, 'DW1 SPU trace', guarded(function(address, width)
        if not audio then return true end
        local r, ram = PCSX.getRegisters(), PCSX.getMemPtr()
        local instruction
        for _, pc in ipairs({tonumber(r.rawPC), tonumber(r.rawPC)-4}) do
            local p = bit.band(pc, 0x1fffff)
            local code = tonumber(ram[p]) + tonumber(ram[p+1])*256 +
                         tonumber(ram[p+2])*65536 + tonumber(ram[p+3])*16777216
            local op = bit.rshift(code, 26)
            local immediate = bit.band(code, 65535)
            if immediate >= 32768 then immediate = immediate-65536 end
            local effective = bit.band(tonumber(r.GPR.r[bit.band(bit.rshift(code,21),31)]) + immediate, 0x1fffffff)
            if effective == bit.band(address, 0x1fffffff) and
               ((op == 0x29 and width == 2) or (op == 0x2b and width == 4)) then
                instruction = code
                break
            end
        end
        assert(instruction, 'SPU write is not a decoded SH/SW at rawPC or rawPC-4')
        local value = tonumber(r.GPR.r[bit.band(bit.rshift(instruction,16),31)])
        for i=0,width-1,2 do
            local off = bit.band(address,0x1fffffff)-0x1f801c00+i
            assert(off >= 0 and off < 0x200 and off%2 == 0)
            audioWrite(off, bit.band(bit.rshift(value,i*8),65535))
        end
        return true
    end))
end
-- Exercise the actual override API before guest execution, then restore released inputs.
local pad = PCSX.SIO0.slots[1].pads[1]
for button = 0, 15 do pad.clearOverride(button) end
for button = 0, 15 do
    pad.setOverride(button)
    for other = 0, 15 do
        assert(pad.getButton(other) == (other == button), 'pad override bit mismatch')
    end
    pad.clearOverride(button)
    assert(not pad.getButton(button), 'pad override did not clear')
end
local function input(k)
    if #checkpoints > 0 then
        for _, event in ipairs(c.inputs) do
            if not event.applied then
                for _, cp in ipairs(checkpoints) do
                    if cp.label == event.after and cp.start and ticks - cp.start >= event.vblank then
                        held = event.buttons
                        event.applied = true
                        if hardwarePoint then
                            local mask=0
                            for _, button in ipairs(held) do mask=bit.bor(mask,bit.lshift(1,button)) end
                            hardwarePoint('input',event.after,string.format(',"relative":%d,"mask":%d',event.vblank,mask))
                        end
                        break
                    end
                end
            end
        end
    else
        while c.inputs[nextInput] and c.inputs[nextInput].vblank <= k do
            held = c.inputs[nextInput].buttons
            nextInput = nextInput + 1
        end
    end
    -- Reapply the held state at every latch, including after a guest pad reset.
    local pad = PCSX.SIO0.slots[1].pads[1]
    for button = 0, 15 do pad.clearOverride(button) end
    for _, button in ipairs(held) do pad.setOverride(button) end
    if c.input_diagnostics and hardwarePoint then
        local mask=0
        for button=0,15 do if pad.getButton(button) then mask=bit.bor(mask,bit.lshift(1,button)) end end
        local ram=ffi.cast('const uint8_t*',PCSX.getMemPtr())
        hardwarePoint('pad_latch','pad',string.format(',"mask":%d,"packet":%d,"raw_pad":%d',mask,
            tonumber(ram[0x135450])+256*tonumber(ram[0x135451])+65536*tonumber(ram[0x135452])+16777216*tonumber(ram[0x135453]),
            -- Verified s_padBuffer1 in the installed, seed-patched OpenBIOS.
            tonumber(ram[0xaea8])+256*tonumber(ram[0xaea9])+65536*tonumber(ram[0xaeaa])+16777216*tonumber(ram[0xaeab])))
    end
end
progress('before frame 0')
-- AD-15 frame 0: return of the game's first VSync call (nothing calls VSync before main).
breakpoints.vsync = PCSX.addBreakpoint(c.vsync, 'Exec', 4, 'DW1 first VSync', guarded(function()
    -- The pinned event binding retains stack entries on its listener coroutine.
    -- Renew it from guest execution, after the previous event callback returned.
    if ticks - listenerEpoch >= 4096 then renewListener() end
    local ra = tonumber(PCSX.getRegisters().GPR.n.ra)
    if not origin and not breakpoints.returned then
        breakpoints.returned = PCSX.addBreakpoint(ra, 'Exec', 4, 'DW1 frame 0', guarded(function()
            if not origin then
                origin = ticks
                if timing then
                    originCycle = accountCycles()
                    for _, name in ipairs(costNames) do costBase[name] = costs[name] end
                    timingPoint('frame0', 'frame0')
                end
                progress('frame 0')
                if c.inputs_from ~= 'mark' then input(0) end
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
        timingPoint('mark', c.mark_name or 'mark')
        progress('mark')
        if #checkpoints == 0 then breakpoints.mark:disable() end
    end
    enterCheckpoints(c.mark)
    return true
end))
if state then
    breakpoints.rand = PCSX.addBreakpoint(c.rand, 'Exec', 4, 'DW1 rand', guarded(function()
        if mark and not complete() then
            local ra = tonumber(PCSX.getRegisters().GPR.n.ra)
            local key = 'rand_' .. ra
            if not breakpoints[key] then
                breakpoints[key] = PCSX.addBreakpoint(ra, 'Exec', 4, 'DW1 rand return', guarded(function()
                    local value = tonumber(PCSX.getRegisters().GPR.n.v0)
                    assert(state:write(string.format('%d rand %d %d\n', ticks-mark, randIndex, value)))
                    assert(state:flush())
                    randIndex = randIndex + 1
                    breakpoints[key]:disable()
                    return true
                end))
            else breakpoints[key]:enable() end
        end
        return true
    end))
end
snapshot = function(label, relative)
    if state then
        local ram = PCSX.getMemPtr()
        for _, var in ipairs(c.state_vars) do
            for i = 0, var.count - 1 do
                local address = bit.band(var.address, 0x1fffff) + i * var.width
                local value = 0
                for j = 0, var.width - 1 do value = value + tonumber(ram[address + j]) * 2^(8*j) end
                if var.type == 'ptr' then
                    if value == 0 then value = '0'
                    else
                        assert(value < 0x200000 or
                               (value >= 0x80000000 and value < 0x80200000) or
                               (value >= 0xa0000000 and value < 0xa0200000),
                               string.format('unresolved reference pointer: 0x%08x', value))
                        local normalized = bit.bor(bit.band(value, 0x1fffff), 0x80000000)
                        if normalized < 0 then normalized = normalized + 4294967296 end
                        local found
                        for _, symbol in ipairs(c.state_symbols) do
                            if normalized >= symbol.address and normalized < symbol.address + symbol.size then
                                found = symbol.name .. '+' .. (normalized - symbol.address)
                                break
                            end
                        end
                        value = found or string.format('0x%08x', normalized)
                    end
                elseif var.type:sub(1, 1) == 's' and value >= 2^(8*var.width-1) then
                    value = value - 2^(8*var.width)
                end
                local prefix = label and string.format('%s %d %d', label, relative, ticks-mark) or tostring(ticks-mark)
                assert(state:write(string.format('%s state %s[%d] %s\n', prefix, var.name, i, tostring(value))))
            end
        end
        assert(state:flush())
    end
end
local byAddress, audioStops = {}, {}
for _, segment in ipairs(c.audio or {}) do
    for _, entry in ipairs(segment.stop_addresses or {}) do
        audioStops[entry.address] = entry.label
        byAddress[entry.address] = byAddress[entry.address] or {}
    end
end
for _, cp in ipairs(checkpoints) do
    if not byAddress[cp.address] then byAddress[cp.address] = {} end
    table.insert(byAddress[cp.address], cp)
end
enterCheckpoints = function(address)
    if not mark then return end
    if audio and audioStops[address] then
        for _, entry in ipairs(audio.stop_addresses or {}) do
            if entry.address == address then audioEnd('event:' .. entry.label); break end
        end
    end
    for _, cp in ipairs(byAddress[address] or {}) do
        local armed = not cp.after
        for _, previous in ipairs(checkpoints) do
            if previous.label == cp.after and previous.start then armed = true end
        end
        if armed then cp.hits = (cp.hits or 0) + 1 end
        if armed and cp.hits == cp.occurrence then
            assert(checkpoints[checkpointIndex] == cp, 'checkpoint out of order: ' .. cp.label)
            cp.start = ticks
            timingPoint('checkpoint', cp.label)
            checkpointIndex = checkpointIndex + 1
            audioCheckpoint(cp.label)
            snapshot(cp.label, 0)
            progress('checkpoint ' .. cp.label)
        end
    end
end
for address, _ in pairs(byAddress) do
    if address ~= c.mark then
        breakpoints['checkpoint_' .. address] = PCSX.addBreakpoint(address, 'Exec', 4, 'DW1 checkpoint', guarded(function()
            enterCheckpoints(address)
            return true
        end))
    end
end
local onVsync = guarded(function()
    ticks = ticks + 1
    if origin and ticks-origin <= 256 then timingPoint('vblank', 'vblank') end
    if audio and audio.vblanks and ticks-audio.tick >= audio.vblanks then audioEnd("vblanks") end
    local base = c.inputs_from == 'mark' and mark or (c.inputs_from ~= 'mark' and origin or nil)
    if base then input(ticks - base) end
    if mark then
        if #checkpoints == 0 then snapshot() else
            for _, cp in ipairs(checkpoints) do
                if cp.start and ticks > cp.start and ticks - cp.start <= cp.window then
                    snapshot(cp.label, ticks - cp.start)
                end
            end
        end
    end
    if c.audio and audioIndex > #c.audio then
        -- Let the asynchronous mixer flush through the recorded end cycle.
        -- These later VBlanks belong to neither segment.
        if ticks - audioFinishedTick >= (c.audio_drain_vblanks or 0) and
           (#checkpoints == 0 or complete()) then
            progress('audio captured')
            PCSX.quit(0)
        end
    elseif not c.audio and mark and ((#checkpoints > 0 and complete()) or (#checkpoints == 0 and ticks - mark >= c.capture_vblank)) then
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
        if state then assert(state:close()) end
        if shopFinish then shopFinish() end
        progress('captured')
        PCSX.quit(0)
    elseif ticks >= c.max_vblanks then
        progress(mark and 'timeout after mark' or (origin and 'timeout after frame 0' or 'timeout before frame 0'))
        PCSX.quit(1)
    end
end)
renewListener = function()
    if listener then listener:remove() end
    listener = PCSX.Events.createEventListener('GPU::Vsync', onVsync)
    listenerEpoch = ticks
    if DW1_REFERENCE_HANDLES then DW1_REFERENCE_HANDLES[2] = listener end
end
renewListener()
-- Keep handles reachable even if the host collects the script's locals.
DW1_REFERENCE_HANDLES = {breakpoints, listener}
