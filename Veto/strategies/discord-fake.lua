-- Veto Strategy: Discord Bypass
-- Uses fake packets with low TTL to confuse DPI

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

    if not string.find(sni, "discord") then
        return VERDICT_PASS
    end

    local fake = veto.make_fake(pkt.data, 4)

    return {
        verdict = VERDICT_FAKE,
        fake = fake
    }
end
