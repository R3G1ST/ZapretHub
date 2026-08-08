-- Veto Strategy: Reddit DPI Bypass
-- Targets Reddit, Reddit media, Reddit CDN

DOMAINS = {
    "reddit.com",
    "redd.it",
    "redditstatic.com",
    "redditmedia.com",
    "i.redd.it",
    "v.redd.it",
    "old.reddit.com",
    "www.reddit.com",
    "api.reddit.com",
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

    local fake = veto.make_fake(data, 5)
    return {
        verdict = VERDICT_FAKE,
        fake = fake
    }
end
