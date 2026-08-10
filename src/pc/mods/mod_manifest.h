#pragma once

#include "mod.h"

#define MOD_MANIFEST_ENTRY_FILE "manifest.json"

char *mod_manifest_get_entry_file_path(const char *path);
char **mod_manifest_get_array_of_string(struct Mod *mod, const char *key);
char *mod_manifest_get_string(struct Mod *mod, const char *key);
bool mod_manifest_get_bool(struct Mod *mod, const char *key, bool defaultValue);
