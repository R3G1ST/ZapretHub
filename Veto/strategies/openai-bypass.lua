-- Veto Strategy: ChatGPT / OpenAI DPI Bypass
-- Targets ChatGPT, OpenAI API, auth, CDN

DOMAINS = {
    "chatgpt.com",
    "openai.com",
    "chat.openai.com",
    "api.openai.com",
    "auth0.openai.com",
    "cdn.oaistatic.com",
    "oaiusercontent.com",
    "ai.com",
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

    local fake = veto.make_fake(data, 4)
    return {
        verdict = VERDICT_FAKE,
        fake = fake
    }
end
