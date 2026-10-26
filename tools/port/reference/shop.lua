-- Optional SHOP investigation; shares capture.lua boot, inputs and frame-0 clock.
return function(config, handles, guarded, clock)
    local bit = require('bit')
    local memory = PCSX.getMemoryAsFile()
    local file = assert(io.open(config.trace, 'w'))
    local stack, counts, loads, active = {}, {Exec={}, Read={}}, 0, false
    local function emit(event, fields)
        local vblank, cycle = clock()
        assert(file:write(string.format('{"event":"%s","vblank":%d,"cycle":%.0f%s}\n',
            event, vblank, cycle, fields or '')))
        assert(file:flush())
    end
    local function watch(address, kind, width, key, callback)
        handles['shop_'..key] = PCSX.addBreakpoint(address,kind,width,'DW1 SHOP '..key,guarded(callback))
    end
    local function traceStack()
        local rows = {}
        for _, frame in ipairs(stack) do
            rows[#rows+1] = string.format('{"function":"%s","ra":%.0f}',frame.name,frame.ra)
        end
        return '['..table.concat(rows,',')..']'
    end
    for _, kind in ipairs({'Exec','Read'}) do
        watch(0x80080840,kind,0x7dd0,'region_'..kind,function()
            if active then
                local pc = tonumber(PCSX.getRegisters().rawPC)
                counts[kind][pc] = (counts[kind][pc] or 0)+1
            end
            return true
        end)
    end
    for name, address in pairs(config.functions) do
        watch(address,'Exec',4,name,function()
            local r = PCSX.getRegisters().GPR.n
            local ra, a0 = tonumber(r.ra), tonumber(r.a0)
            -- Only track read/trigger/device calls nested in a monitored game scope.
            if (name=='readFile' or name=='isTriggerSet' or name=='CdRead') and #stack==0 then return true end
            local frame = {name=name,ra=ra,loaded=false,lib=a0}
            stack[#stack+1] = frame
            if name=='checkShopMap' then
                emit('check',string.format(',"map":%d,"ra":%.0f,"stack":%s',a0,ra,traceStack()))
            elseif name=='loadDynamicLibrary' and a0==11 then
                loads, active = loads+1, true
                for _, scope in ipairs(stack) do if scope.name=='checkShopMap' then scope.loaded=true end end
                emit('load',string.format(',"lib":11,"ra":%.0f,"destination":2148009984,"file_size":32272,"async":%d,"stack":%s',
                    ra,tonumber(r.a2),traceStack()))
            elseif name=='readFile' and active and stack[#stack-1] and stack[#stack-1].name=='loadDynamicLibrary' and stack[#stack-1].lib==11 then
                assert(memory:readU32At(a0)==0x504f4853, 'unexpected SHOP path') -- "SHOP"
                assert(tonumber(r.a1)==0x80080800,'unexpected SHOP destination')
                emit('read_file',string.format(',"ra":%.0f,"destination":%.0f,"file_size":32272,"stack":%s',ra,tonumber(r.a1),traceStack()))
            elseif name=='CdRead' and active and tonumber(r.a1)==0x80080800 then
                emit('cd_read',string.format(',"ra":%.0f,"destination":%.0f,"sectors":%d,"transfer_size":%d,"stack":%s',
                    ra,tonumber(r.a1),a0,a0*2048,traceStack()))
            end
            local key='return_'..ra
            if not handles['shop_'..key] then
                watch(ra,'Exec',4,key,function()
                    while #stack>0 and stack[#stack].ra==ra do
                        local scope=table.remove(stack)
                        if scope.name=='isTriggerSet' and stack[#stack] and stack[#stack].name=='checkShopMap' then
                            stack[#stack].trigger=tonumber(PCSX.getRegisters().GPR.n.v0)
                        elseif scope.name=='checkShopMap' then
                            emit(scope.loaded and 'check_loaded' or 'skip',string.format(',"ra":%.0f,"trigger_0x29":%d',ra,scope.trigger or -1))
                        end
                    end
                    return true
                end)
            end
            return true
        end)
    end
    return function()
        for _, kind in ipairs({'Exec','Read'}) do
            local pcs, total = {}, 0
            for pc in pairs(counts[kind]) do pcs[#pcs+1]=pc end
            table.sort(pcs)
            for _, pc in ipairs(pcs) do
                total=total+counts[kind][pc]
                emit('access',string.format(',"kind":"%s","pc":%.0f,"count":%d',kind,pc,counts[kind][pc]))
            end
            emit('summary',string.format(',"kind":"%s","count":%d,"loads":%d,"start":2148010048,"end_exclusive":2148042256',kind,total,loads))
        end
        assert(file:close())
    end
end
