#include "config.h"
#include "utils.h"
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Config* load_config(const char *config_file) {
    Config *config = NULL;
    config_t cfg;
    config_setting_t *categories_setting;
    
    // Initialize config structure
    config = malloc(sizeof(Config));
    if (!config) {
        log_message(LOG_ERROR, "Failed to allocate memory for configuration");
        return NULL;
    }
    
    memset(config, 0, sizeof(Config));
    
    // Set default values
    config->default_category = safe_strdup("Uncategorised");
    if (!config->default_category) {
        free(config);
        return NULL;
    }
    
    // If no config file specified, return default config
    if (!config_file) {
        log_message(LOG_INFO, "No configuration file specified, using defaults");
        return config;
    }
    
    // Initialize libconfig
    config_init(&cfg);
    
    // Read the configuration file
    if (!config_read_file(&cfg, config_file)) {
        log_message(LOG_ERROR, "Configuration file error: %s:%d - %s",
                   config_error_file(&cfg),
                   config_error_line(&cfg),
                   config_error_text(&cfg));
        config_destroy(&cfg);
        free_config(config);
        return NULL;
    }
    
    // Read default_category if present
    const char *default_cat;
    if (config_lookup_string(&cfg, "default_category", &default_cat)) {
        free(config->default_category);
        config->default_category = safe_strdup(default_cat);
        if (!config->default_category) {
            config_destroy(&cfg);
            free_config(config);
            return NULL;
        }
    }
    
    // Read categories array
    categories_setting = config_lookup(&cfg, "categories");
    if (categories_setting) {
        int count = config_setting_length(categories_setting);
        if (count > 0) {
            config->rules = malloc(count * sizeof(CategoryRule));
            if (!config->rules) {
                log_message(LOG_ERROR, "Failed to allocate memory for category rules");
                config_destroy(&cfg);
                free_config(config);
                return NULL;
            }
            
            config->rule_count = 0;
            
            for (int i = 0; i < count; i++) {
                config_setting_t *rule = config_setting_get_elem(categories_setting, i);
                if (!rule) continue;
                
                const char *pattern, *category;
                if (!config_setting_lookup_string(rule, "pattern", &pattern) ||
                    !config_setting_lookup_string(rule, "category", &category)) {
                    log_message(LOG_WARN, "Skipping invalid category rule at index %d", i);
                    continue;
                }
                
                // Allocate and copy strings
                config->rules[config->rule_count].pattern = safe_strdup(pattern);
                config->rules[config->rule_count].category = safe_strdup(category);
                
                if (!config->rules[config->rule_count].pattern || 
                    !config->rules[config->rule_count].category) {
                    log_message(LOG_ERROR, "Failed to allocate memory for rule %d", i);
                    // Clean up and continue
                    safe_free((void**)&config->rules[config->rule_count].pattern);
                    safe_free((void**)&config->rules[config->rule_count].category);
                    continue;
                }
                
                // Compile regex
                int errorcode;
                PCRE2_SIZE erroroffset;
                config->rules[config->rule_count].compiled_regex = pcre2_compile_8(
                    (PCRE2_SPTR8)pattern,
                    PCRE2_ZERO_TERMINATED,
                    PCRE2_CASELESS | PCRE2_UTF,
                    &errorcode,
                    &erroroffset,
                    NULL
                );
                
                if (!config->rules[config->rule_count].compiled_regex) {
                    PCRE2_UCHAR8 buffer[256];
                    pcre2_get_error_message_8(errorcode, buffer, sizeof(buffer));
                    log_message(LOG_ERROR, "Failed to compile regex '%s': %s", pattern, (char*)buffer);
                    
                    // Clean up this rule
                    safe_free((void**)&config->rules[config->rule_count].pattern);
                    safe_free((void**)&config->rules[config->rule_count].category);
                    continue;
                }
                
                config->rule_count++;
                log_message(LOG_INFO, "Loaded category rule: '%s' -> '%s'", pattern, category);
            }
        }
    }
    
    // Read parser configurations
    config_setting_t *parsers_setting = config_lookup(&cfg, "parsers");
    if (parsers_setting) {
        // Read ANZ parser config
        config_setting_t *anz_setting = config_setting_get_member(parsers_setting, "anz");
        if (anz_setting) {
            config->anz_config = malloc(sizeof(ParserConfig));
            if (config->anz_config) {
                memset(config->anz_config, 0, sizeof(ParserConfig));
                const char *method, *provider;
                if (config_setting_lookup_string(anz_setting, "method", &method)) {
                    config->anz_config->method = safe_strdup(method);
                }
                if (config_setting_lookup_string(anz_setting, "provider", &provider)) {
                    config->anz_config->provider = safe_strdup(provider);
                }
            }
        }
        
        // Read CBA parser config
        config_setting_t *cba_setting = config_setting_get_member(parsers_setting, "cba");
        if (cba_setting) {
            config->cba_config = malloc(sizeof(ParserConfig));
            if (config->cba_config) {
                memset(config->cba_config, 0, sizeof(ParserConfig));
                const char *method, *provider;
                if (config_setting_lookup_string(cba_setting, "method", &method)) {
                    config->cba_config->method = safe_strdup(method);
                }
                if (config_setting_lookup_string(cba_setting, "provider", &provider)) {
                    config->cba_config->provider = safe_strdup(provider);
                }
            }
        }
    }
    
    // Read AI provider configurations
    config_setting_t *ai_providers_setting = config_lookup(&cfg, "ai_providers");
    if (ai_providers_setting) {
        // Read Anthropic provider config
        config_setting_t *anthropic_setting = config_setting_get_member(ai_providers_setting, "anthropic");
        if (anthropic_setting) {
            config->anthropic_config = malloc(sizeof(AIProviderConfig));
            if (config->anthropic_config) {
                memset(config->anthropic_config, 0, sizeof(AIProviderConfig));
                const char *api_key_env, *base_url, *model;
                if (config_setting_lookup_string(anthropic_setting, "api_key_env", &api_key_env)) {
                    config->anthropic_config->api_key_env = safe_strdup(api_key_env);
                }
                if (config_setting_lookup_string(anthropic_setting, "base_url", &base_url)) {
                    config->anthropic_config->base_url = safe_strdup(base_url);
                }
                if (config_setting_lookup_string(anthropic_setting, "model", &model)) {
                    config->anthropic_config->model = safe_strdup(model);
                }
            }
        }
        
        // Read OpenRouter provider config
        config_setting_t *openrouter_setting = config_setting_get_member(ai_providers_setting, "openrouter");
        if (openrouter_setting) {
            config->openrouter_config = malloc(sizeof(AIProviderConfig));
            if (config->openrouter_config) {
                memset(config->openrouter_config, 0, sizeof(AIProviderConfig));
                const char *api_key_env, *base_url, *model;
                if (config_setting_lookup_string(openrouter_setting, "api_key_env", &api_key_env)) {
                    config->openrouter_config->api_key_env = safe_strdup(api_key_env);
                }
                if (config_setting_lookup_string(openrouter_setting, "base_url", &base_url)) {
                    config->openrouter_config->base_url = safe_strdup(base_url);
                }
                if (config_setting_lookup_string(openrouter_setting, "model", &model)) {
                    config->openrouter_config->model = safe_strdup(model);
                }
            }
        }
        
        // Read Llama.cpp provider config  
        config_setting_t *llamacpp_setting = config_setting_get_member(ai_providers_setting, "llamacpp");
        if (llamacpp_setting) {
            config->llamacpp_config = malloc(sizeof(AIProviderConfig));
            if (config->llamacpp_config) {
                memset(config->llamacpp_config, 0, sizeof(AIProviderConfig));
                const char *api_key_env, *base_url, *model;
                if (config_setting_lookup_string(llamacpp_setting, "api_key_env", &api_key_env)) {
                    config->llamacpp_config->api_key_env = safe_strdup(api_key_env);
                }
                if (config_setting_lookup_string(llamacpp_setting, "base_url", &base_url)) {
                    config->llamacpp_config->base_url = safe_strdup(base_url);
                }
                if (config_setting_lookup_string(llamacpp_setting, "model", &model)) {
                    config->llamacpp_config->model = safe_strdup(model);
                }
            }
        }
    }
    
    config_destroy(&cfg);
    
    log_message(LOG_INFO, "Configuration loaded: %zu category rules", config->rule_count);
    return config;
}

void free_config(Config *cfg) {
    if (!cfg) return;
    
    safe_free((void**)&cfg->default_category);
    
    if (cfg->rules) {
        for (size_t i = 0; i < cfg->rule_count; i++) {
            safe_free((void**)&cfg->rules[i].pattern);
            safe_free((void**)&cfg->rules[i].category);
            if (cfg->rules[i].compiled_regex) {
                pcre2_code_free_8(cfg->rules[i].compiled_regex);
            }
        }
        free(cfg->rules);
    }
    
    // Free parser configurations
    if (cfg->anz_config) {
        safe_free((void**)&cfg->anz_config->method);
        safe_free((void**)&cfg->anz_config->provider);
        free(cfg->anz_config);
    }
    
    if (cfg->cba_config) {
        safe_free((void**)&cfg->cba_config->method);
        safe_free((void**)&cfg->cba_config->provider);
        free(cfg->cba_config);
    }
    
    // Free AI provider configurations
    if (cfg->anthropic_config) {
        safe_free((void**)&cfg->anthropic_config->api_key_env);
        safe_free((void**)&cfg->anthropic_config->base_url);
        safe_free((void**)&cfg->anthropic_config->model);
        free(cfg->anthropic_config);
    }
    
    if (cfg->openrouter_config) {
        safe_free((void**)&cfg->openrouter_config->api_key_env);
        safe_free((void**)&cfg->openrouter_config->base_url);
        safe_free((void**)&cfg->openrouter_config->model);
        free(cfg->openrouter_config);
    }
    
    if (cfg->llamacpp_config) {
        safe_free((void**)&cfg->llamacpp_config->api_key_env);
        safe_free((void**)&cfg->llamacpp_config->base_url);
        safe_free((void**)&cfg->llamacpp_config->model);
        free(cfg->llamacpp_config);
    }
    
    free(cfg);
}
