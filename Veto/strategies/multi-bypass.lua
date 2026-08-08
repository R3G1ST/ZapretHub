-- Veto Strategy: General DPI Bypass
-- Multi-domain strategy with configurable domains

DOMAINS = {
    "youtube.com",
    "googlevideo.com",
    "ytimg.com",
    "discord.com",
    "discord.gg",
    "discordapp.com",
    "twitter.com",
    "x.com",
    "t.co",
}

function contains_domain(sni, domains)
    if not sni then return false end
    for _, domain in ipairs(domains) do
        if string.find(sni, domain, 1, true) then
            return true
        end
    end
    return false
end

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

    if not contains_domain(proto.sni, DOMAINS) then
        return VERDICT_PASS
    end

    local data = pkt.data
    if not data or #data < 50 then
        return VERDICT_PASS
    end

    if string.find(proto.sni, "discord") then
        local fake = veto.make_fake(data, 4)
        return {
            verdict = VERDICT_FAKE,
            fake = fake
        }
    else
        local split_pos = math.min(30, math.floor(#data / 3))
        local part1, part2 = veto.split_packet(data, split_pos)
        return {
            verdict = VERDICT_MODIFY,
            data = part1
        }
    end
end
