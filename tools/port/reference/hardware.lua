-- Hardware observations in the pinned Redux interpreter, before CPU stores.
-- Device spans may overlap; do not sum them as exclusive CPU/API costs.
return function(path, handles, guarded, source, mapLoad, irq)
    local bit = require('bit')
    local file = assert(io.open(path, 'w'))
    local memory = PCSX.getMemoryAsFile()
    local function emit(device, event, fields)
        assert(file:write(string.format('{"cycle":%.0f,"device":"%s","event":"%s","scope":"%s"%s}\n',
            tonumber(PCSX.getCPUCycles()), device, event, source(), fields or '')))
        assert(file:flush())
    end
    local function watch(address, kind, width, name, callback)
        handles['hardware_'..name] = PCSX.addBreakpoint(address, kind, width, name, guarded(callback))
    end
    local function store(address, width)
        local r = PCSX.getRegisters()
        for _, pc in ipairs({tonumber(r.rawPC), tonumber(r.rawPC)-4}) do
            local code = memory:readU32At(pc)
            local op, immediate = bit.rshift(code,26), bit.band(code,65535)
            if immediate >= 32768 then immediate = immediate-65536 end
            local effective = bit.band(tonumber(r.GPR.r[bit.band(bit.rshift(code,21),31)])+immediate,0x1fffffff)
            if effective == bit.band(address,0x1fffffff) and
                ((op==0x28 and width==1) or (op==0x29 and width==2) or (op==0x2b and width==4)) then
                local value = tonumber(r.GPR.r[bit.band(bit.rshift(code,16),31)])
                return width==1 and bit.band(value,255) or width==2 and bit.band(value,65535) or value
            end
        end
        error(string.format('undecoded hardware store at %08x width %d',address,width))
    end
    local sioActive, sioPad
    watch(0x1f801070,'Write',8,'IRQ routing',function(address,width)
        emit('cpu',bit.band(address,4)==0 and 'irq_ack' or 'irq_mask',
            string.format(',"value":%.0f,"pc":%.0f',store(address,width),
                tonumber(PCSX.getRegisters().rawPC)))
        return true
    end)
    watch(0x1f801120,'Write',12,'Timer 2 configuration',function(address,width)
        emit('cpu','timer2_config',string.format(',"offset":%d,"value":%.0f',
            bit.band(address,15),store(address,width)))
        return true
    end)
    if irq and irq.trapIntr and irq.ReturnFromException then
        local trap
        watch(irq.trapIntr,'Exec',4,'PsyQ IRQ entry',function()
            trap={start=tonumber(PCSX.getCPUCycles()),pending=memory:readU16At(0x1f801070)}
            return true
        end)
        watch(irq.ReturnFromException,'Exec',4,'PsyQ IRQ return',function()
            if trap then
                emit('cpu','irq_span',string.format(',"start":%.0f,"elapsed":%.0f,"pending":%d',
                    trap.start,tonumber(PCSX.getCPUCycles())-trap.start,trap.pending))
                trap=nil
            end
            return true
        end)
    end
    watch(0x1f801040,'Write',16,'SIO0 card transaction',function(address,width)
        local offset, value = bit.band(address,15), store(address,width)
        if offset==0 then
            if value==0x81 and not sioActive then
                sioActive={start=tonumber(PCSX.getCPUCycles()),bytes=1}
            elseif sioActive then sioActive.bytes=sioActive.bytes+1 end
            if value==1 and not sioActive and not sioPad then
                sioPad={start=tonumber(PCSX.getCPUCycles()),bytes=1}
            elseif sioPad then sioPad.bytes=sioPad.bytes+1 end
        elseif offset==10 and value==0 and sioActive then
            emit('card','serial_transaction',string.format(',"start":%.0f,"elapsed":%.0f,"bytes":%d',
                sioActive.start,tonumber(PCSX.getCPUCycles())-sioActive.start,sioActive.bytes))
            sioActive=nil
        end
        if offset==10 and value==0 and sioPad then
            emit('cpu','pad_poll',string.format(',"start":%.0f,"elapsed":%.0f,"bytes":%d',
                sioPad.start,tonumber(PCSX.getCPUCycles())-sioPad.start,sioPad.bytes))
            sioPad=nil
        end
        return true
    end)
    local bank, parameters = 0, {}
    watch(0x1f801800,'Write',4,'CD command',function(address,width)
        assert(width==1, 'CD register store must be byte sized')
        local off, value = bit.band(address,3), store(address,width)
        if off==0 then bank=bit.band(value,3)
        elseif off==2 and bank==0 then parameters[#parameters+1]=value
        elseif off==1 and bank==0 then
            emit('cd','command',string.format(',"command":%d,"parameters":[%s]',value,table.concat(parameters,',')))
            parameters={}
        elseif off==3 and bank==1 and bit.band(value,7)~=0 then
            emit('cd','ack',string.format(',"mask":%d',value))
        end
        return true
    end)
    -- getintr: after LBU reg3, its delay slot and ANDI 7 (verified USA asm).
    -- This observes the real IRQ value without reading the CD device twice.
    watch(0x800b060c,'Exec',4,'CD IRQ',function()
        local irq=tonumber(PCSX.getRegisters().GPR.n.v0)
        if irq~=0 then emit('cd','irq',string.format(',"irq":%d',irq)) end
        return true
    end)
    local packet, remaining, polyline = {}, 0, false
    local function gp0(value)
        if remaining>0 then remaining=remaining-1;return end
        if polyline then
            if bit.band(value,0xf000f000)==0x50005000 then polyline=false end
            return
        end
        packet[#packet+1]=value
        local op=bit.rshift(packet[1],24)
        local length=1
        if op==2 then length=3
        elseif op>=0x20 and op<0x40 then
            local n=bit.band(op,8)~=0 and 4 or 3
            length=1+n*(bit.band(op,4)~=0 and 2 or 1)+(bit.band(op,16)~=0 and n-1 or 0)
        elseif op>=0x40 and op<0x60 then length=bit.band(op,16)~=0 and 4 or 3
        elseif op>=0x60 and op<0x80 then
            length=2+(bit.band(op,4)~=0 and 1 or 0)+(bit.band(bit.rshift(op,3),3)==0 and 1 or 0)
        elseif op>=0x80 and op<0xa0 then length=4
        elseif op>=0xa0 and op<0xe0 then length=3 end
        if #packet<length then return end
        local fields=string.format(',"command":%d,"words":%d',op,length)
        if op==2 or (op>=0xa0 and op<0xe0) or (op>=0x60 and op<0x80 and bit.band(op,0x18)==0) then
            local size=packet[length]
            fields=fields..string.format(',"width":%d,"height":%d',bit.band(size,65535),bit.rshift(size,16))
        end
        emit('gpu','gp0',fields)
        if op>=0xa0 and op<0xc0 then
            local w=bit.band(packet[3]-1,1023)+1
            local h=bit.band(bit.rshift(packet[3],16)-1,511)+1
            remaining=math.ceil(w*h/2)
        end
        polyline=op>=0x40 and op<0x60 and bit.band(op,8)~=0
        packet={}
    end
    watch(0x1f801810,'Write',8,'GPU command',function(address,width)
        assert(width==4)
        local value=store(address,width)
        if bit.band(address,4)==0 then gp0(value) else
            local op=bit.rshift(value,24)
            emit('gpu','gp1',string.format(',"command":%d,"value":%.0f',op,value))
            if op==0 or op==1 then packet={};remaining=0;polyline=false end
        end
        return true
    end)
    local mdecCommand=0
    watch(0x1f801820,'Write',8,'MDEC command',function(address,width)
        local value=store(address,width)
        if bit.band(address,4)==0 then mdecCommand=value end
        emit('mdec',bit.band(address,4)==0 and 'command' or 'control',
            string.format(',"value":%.0f',value))
        return true
    end)
    local active={}
    for channel=0,3 do
        local ch=channel
        local base=0x1f801080+ch*16
        local device=ch<2 and 'mdec' or ch==2 and 'gpu' or 'cd'
        watch(base+8,'Write',4,'DMA start '..ch,function(address,width)
            local control=store(address,width)
            if bit.band(control,0x1000000)==0 then return true end
            local madr,bcr=memory:readU32At(base),memory:readU32At(base+4)
            local words=bit.band(bcr,65535)
            if bit.band(control,0x600)~=0 then words=words*bit.rshift(bcr,16) end
            if ch==2 and bit.band(control,1)~=0 then
                local ram=PCSX.getMemPtr()
                local function word(a)
                    a=bit.band(a,0x1ffffc)
                    return tonumber(ram[a])+tonumber(ram[a+1])*256+tonumber(ram[a+2])*65536+tonumber(ram[a+3])*16777216
                end
                if bit.band(control,0x400)~=0 then
                    -- Count GP0 payload words; linked-list headers are excluded (BCR is unused).
                    words=0
                    local next=madr
                    local seen={}
                    while bit.band(next,0x800000)==0 do
                        local a=bit.band(next,0x1ffffc)
                        assert(not seen[a], 'cyclic GPU DMA list');seen[a]=true
                        local tag=word(a)
                        words=words+bit.rshift(tag,24)
                        for i=1,bit.rshift(tag,24) do gp0(word(a+4*i)) end
                        next=bit.band(tag,0xffffff)
                    end
                else
                    for i=0,words-1 do gp0(word(madr+4*i)) end
                end
            end
            emit(device,'dma',string.format(',"channel":%d,"control":%.0f,"address":%.0f,"bcr":%.0f,"words":%d',
                ch,control,madr,bcr,words)..(ch<2 and string.format(',"command":%.0f',mdecCommand) or ''))
            active[ch]={cycle=tonumber(PCSX.getCPUCycles()),last=tonumber(PCSX.getCPUCycles())}
            return true
        end)
        watch(base+8,'Read',4,'DMA completion '..ch,function()
            local span=active[ch]
            if span then
                local cycle=tonumber(PCSX.getCPUCycles())
                if bit.band(memory:readU32At(base+8),0x1000000)==0 then
                    emit(device,'dma_done',string.format(',"channel":%d,"elapsed":%.0f,"previous_busy":%.0f',
                        ch,cycle-span.cycle,span.last-span.cycle))
                    active[ch]=nil
                else span.last=cycle end
            end
            return true
        end)
    end
    -- The end must be observed at loadMap's real caller, not guessed from C order.
    if mapLoad then
        watch(mapLoad,'Exec',4,'map load entry',function()
            local ra=tonumber(PCSX.getRegisters().GPR.n.ra)
            emit('cpu','loadMap_enter',string.format(',"caller":%.0f',ra))
            watch(ra,'Exec',4,'map load return',function()
                emit('cpu','loadMap_return')
                handles['hardware_map load return']:disable()
                return true
            end)
            return true
        end)
    end
    return function(event,label,fields)
        emit('checkpoint',event,',"label":"'..label..'"'..(fields or ''))
    end
end
