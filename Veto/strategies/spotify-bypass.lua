-- Veto Strategy: Spotify DPI Bypass
-- Targets Spotify web, CDN, API

DOMAINS = {
    "spotify.com",
    "scdn.co",
    "spoti.fi",
    "open.spotify.com",
    "play.spotify.com",
    "ap-gew4.spotify.com",
    "ap-gew1.spotify.com",
    "ap-cfg.spotify.com",
    "seed-ssvc.scdn.co",
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

    local split_pos = math.min(32, math.floor(#data / 3))
    local part1, part2 = veto.split_packet(data, split_pos)
    return {
        verdict = VERDICT_MODIFY,
        data = part1
    }
end
