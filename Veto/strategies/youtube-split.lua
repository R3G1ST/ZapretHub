-- Veto Strategy: YouTube Bypass
-- Splits TLS ClientHello to confuse DPI

function process_packet(pkt, proto, direction)
    if direction ~= DIR_OUTGOING then
        return VERDICT_PASS
    end

    if proto.proto ~= PROTO_TLS then
        return VERDICT_PASS
    end

    if not proto.is_client_hello then
        return VERDICT_PASS
    end

    local sni = proto.sni
    if not sni then
        return VERDICT_PASS
    end

    if not string.find(sni, "youtube") and
       not string.find(sni, "googlevideo") and
       not string.find(sni, "ytimg") then
        return VERDICT_PASS
    end

    local data = pkt.data
    if not data or #data < 50 then
        return VERDICT_PASS
    end

    local split_pos = math.min(30, math.floor(#data / 3))
    local part1, part2 = veto.split_packet(data, split_pos)

    return {
        verdict = VERDICT_MODIFY,
        data = part1
    }
end
