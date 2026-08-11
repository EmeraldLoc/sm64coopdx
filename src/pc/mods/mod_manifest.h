#pragma once

#include "pc/utils/cJSON.h"
#include "mod.h"

#define MOD_MANIFEST_ENTRY_FILE "manifest.json"

cJSON *mod_manifest_get_json_for_mod(struct Mod *mod);
void mod_manifest_destroy_json(cJSON *json);
char *mod_manifest_get_entry_file_path(const char *path);
char **mod_manifest_get_array_of_string(cJSON *json, const char *key);
char *mod_manifest_get_string(cJSON *json, const char *key);
bool mod_manifest_get_bool(cJSON *json, const char *key, bool defaultValue);
