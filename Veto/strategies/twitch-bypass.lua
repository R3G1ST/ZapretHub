-- Veto Strategy: Twitch DPI Bypass
-- Targets Twitch, Twitch CDN, Twitch edge services

DOMAINS = {
    "twitch.tv",
    "jtvnw.net",
    "ttvnw.net",
    "twitchcdn.net",
    "ext-twitch.tv",
    "usher.ttvnw.net",
    "clips.twitch.tv",
    "vod-metro.twitch.tv",
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

    local split_pos = math.min(35, math.floor(#data / 3))
    local part1, part2 = veto.split_packet(data, split_pos)
    return {
        verdict = VERDICT_MODIFY,
        data = part1
    }
end
