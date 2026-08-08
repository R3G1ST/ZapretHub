#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

veto_config *veto_config_create(void) {
    veto_config *cfg = calloc(1, sizeof(veto_config));
    if (!cfg) return NULL;
    cfg->listen_port = 0;
    cfg->reassembly_timeout_ms = 30000;
    cfg->max_streams = 4096;
    cfg->verbose = false;
    cfg->daemon = false;
    return cfg;
}

void veto_config_destroy(veto_config *cfg) {
    if (!cfg) return;
    for (size_t i = 0; i < cfg->profile_count; i++) {
        veto_profile *p = cfg->profiles[i];
        if (p) {
            for (size_t j = 0; j < p->strategy_count; j++) {
                if (p->strategies[j]) {
                    veto_attack_config_destroy(p->strategies[j]->attacks);
                    free(p->strategies[j]);
                }
            }
            free(p->strategies);
            free(p);
        }
    }
    free(cfg->profiles);
    free(cfg);
}

veto_config *veto_config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    veto_config *cfg = veto_config_create();
    if (!cfg) { fclose(f); return NULL; }

    char line[512];
    veto_profile *current_profile = NULL;
    veto_strategy *current_strategy = NULL;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                const char *section = line + 1;

                if (strcmp(section, "general") == 0) {
                    current_profile = NULL;
                    current_strategy = NULL;
                } else if (strncmp(section, "profile:", 8) == 0) {
                    veto_profile *p = calloc(1, sizeof(veto_profile));
                    if (p) {
                        strncpy(p->name, section + 8, sizeof(p->name) - 1);
                        p->enabled = true;
                        veto_config_add_profile(cfg, p);
                        current_profile = p;
                        current_strategy = NULL;
                    }
                } else if (strncmp(section, "strategy:", 9) == 0 && current_profile) {
                    veto_strategy *s = calloc(1, sizeof(veto_strategy));
                    if (s) {
                        strncpy(s->name, section + 9, sizeof(s->name) - 1);
                        s->enabled = true;
                        s->attacks = veto_attack_config_create();
                        veto_config_add_strategy(current_profile, s);
                        current_strategy = s;
                    }
                }
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        if (current_profile == NULL && current_strategy == NULL) {
            if (strcmp(key, "listen_port") == 0) {
                cfg->listen_port = (uint16_t)atoi(value);
            } else if (strcmp(key, "max_streams") == 0) {
                cfg->max_streams = (size_t)atoi(value);
            } else if (strcmp(key, "timeout_ms") == 0) {
                cfg->reassembly_timeout_ms = (uint32_t)atoi(value);
            } else if (strcmp(key, "verbose") == 0) {
                cfg->verbose = (strcmp(value, "true") == 0);
            }
        } else if (current_strategy && current_strategy->attacks) {
            if (strcmp(key, "type") == 0) {
                int idx = current_strategy->attacks->rule_count;
                if (idx < 16) {
                    current_strategy->attacks->rules[idx].enabled = true;
                    current_strategy->attacks->rule_count++;
                    if (strcmp(value, "fake") == 0) {
                        current_strategy->attacks->rules[idx].type = ATTACK_FAKE;
                    } else if (strcmp(value, "split") == 0) {
                        current_strategy->attacks->rules[idx].type = ATTACK_SPLIT;
                    } else if (strcmp(value, "disorder") == 0) {
                        current_strategy->attacks->rules[idx].type = ATTACK_DISORDER;
                    } else if (strcmp(value, "fakedsplit") == 0) {
                        current_strategy->attacks->rules[idx].type = ATTACK_FAKED_SPLIT;
                    }
                }
            } else if (strcmp(key, "fool") == 0) {
                int idx = current_strategy->attacks->rule_count - 1;
                if (idx >= 0 && idx < 16) {
                    if (strcmp(value, "ttl") == 0) {
                        current_strategy->attacks->rules[idx].fool = FOOL_TTL;
                    } else if (strcmp(value, "badseq") == 0) {
                        current_strategy->attacks->rules[idx].fool = FOOL_BAD_SEQ;
                    } else if (strcmp(value, "badsum") == 0) {
                        current_strategy->attacks->rules[idx].fool = FOOL_BAD_SUM;
                    }
                }
            } else if (strcmp(key, "fake_ttl") == 0) {
                int idx = current_strategy->attacks->rule_count - 1;
                if (idx >= 0 && idx < 16) {
                    current_strategy->attacks->rules[idx].fake_ttl = (uint8_t)atoi(value);
                }
            } else if (strcmp(key, "split_pos") == 0) {
                int idx = current_strategy->attacks->rule_count - 1;
                if (idx >= 0 && idx < 16) {
                    current_strategy->attacks->rules[idx].split_pos[0] = (uint16_t)atoi(value);
                }
            } else if (strcmp(key, "hostlist") == 0) {
                veto_attack_load_hostlist(current_strategy->attacks, value);
            }
        }
    }

    fclose(f);
    return cfg;
}

veto_status veto_config_save(const veto_config *cfg, const char *path) {
    if (!cfg || !path) return VETO_ERR_INVALID;

    FILE *f = fopen(path, "w");
    if (!f) return VETO_ERR_IO;

    fprintf(f, "# Veto Configuration\n\n");
    fprintf(f, "[general]\n");
    fprintf(f, "listen_port=%d\n", cfg->listen_port);
    fprintf(f, "max_streams=%zu\n", cfg->max_streams);
    fprintf(f, "timeout_ms=%u\n", cfg->reassembly_timeout_ms);
    fprintf(f, "verbose=%s\n", cfg->verbose ? "true" : "false");
    fprintf(f, "\n");

    for (size_t i = 0; i < cfg->profile_count; i++) {
        const veto_profile *p = cfg->profiles[i];
        if (!p) continue;

        fprintf(f, "[profile:%s]\n", p->name);
        fprintf(f, "enabled=%s\n\n", p->enabled ? "true" : "false");

        for (size_t j = 0; j < p->strategy_count; j++) {
            const veto_strategy *s = p->strategies[j];
            if (!s) continue;

            fprintf(f, "  [strategy:%s]\n", s->name);
            fprintf(f, "  enabled=%s\n", s->enabled ? "true" : "false");

            if (s->attacks) {
                for (int k = 0; k < s->attacks->rule_count; k++) {
                    const veto_attack_rule *r = &s->attacks->rules[k];
                    const char *type_str = "none";
                    switch (r->type) {
                        case ATTACK_FAKE: type_str = "fake"; break;
                        case ATTACK_SPLIT: type_str = "split"; break;
                        case ATTACK_DISORDER: type_str = "disorder"; break;
                        case ATTACK_FAKED_SPLIT: type_str = "fakedsplit"; break;
                        default: break;
                    }
                    fprintf(f, "  type=%s\n", type_str);
                    if (r->fool != FOOL_NONE) {
                        const char *fool_str = "none";
                        switch (r->fool) {
                            case FOOL_TTL: fool_str = "ttl"; break;
                            case FOOL_BAD_SEQ: fool_str = "badseq"; break;
                            case FOOL_BAD_SUM: fool_str = "badsum"; break;
                            default: break;
                        }
                        fprintf(f, "  fool=%s\n", fool_str);
                    }
                    if (r->fake_ttl > 0) fprintf(f, "  fake_ttl=%d\n", r->fake_ttl);
                    if (r->split_pos[0] > 0) fprintf(f, "  split_pos=%d\n", r->split_pos[0]);
                }
            }
            fprintf(f, "\n");
        }
    }

    fclose(f);
    return VETO_OK;
}

veto_status veto_config_add_profile(veto_config *cfg, veto_profile *profile) {
    if (!cfg || !profile) return VETO_ERR_INVALID;
    veto_profile **new_profiles = realloc(cfg->profiles,
        (cfg->profile_count + 1) * sizeof(veto_profile *));
    if (!new_profiles) return VETO_ERR_NOMEM;
    cfg->profiles = new_profiles;
    cfg->profiles[cfg->profile_count++] = profile;
    return VETO_OK;
}

veto_status veto_config_add_strategy(veto_profile *profile, veto_strategy *strategy) {
    if (!profile || !strategy) return VETO_ERR_INVALID;
    veto_strategy **new_strategies = realloc(profile->strategies,
        (profile->strategy_count + 1) * sizeof(veto_strategy *));
    if (!new_strategies) return VETO_ERR_NOMEM;
    profile->strategies = new_strategies;
    profile->strategies[profile->strategy_count++] = strategy;
    return VETO_OK;
}
