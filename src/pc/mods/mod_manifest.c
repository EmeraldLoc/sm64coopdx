#include "types.h"

#include "pc/utils/cJSON.h"
#include "pc/debuglog.h"

#include "mod.h"
#include "mods_utils.h"
#include "mod_manifest.h"

static bool read_file(const char *path, char **outContents, size_t *outSize) {
    // open file with the task to read
    FILE *file = fopen(path, "rb");
    if (!file) { return false; }

    // get size of file
    fseek(file, 0, SEEK_END);
    s64 fileSize = ftell(file);
    if (fileSize <= 0) {
        fclose(file);
        return false;
    }
    size_t size = (size_t)fileSize;
    rewind(file); // reset file cursor

    // allocate file contents with the size discovered above
    char *fileContents = malloc(size);
    if (!fileContents) {
        fclose(file);
        return false;
    }

    // read the data
    size_t readData = fread(fileContents, 1, size, file);
    fclose(file);

    // if we couldnt read the entire file, return early
    if (readData < size) {
        free(fileContents);
        return false;
    }

    *outContents = fileContents;
    *outSize = (size_t)size;

    return true;
}

static cJSON *get_json_from_path(const char *path) {
    // get file contents and size
    char *fileContents = NULL;
    size_t size = 0;

    if (!read_file(path, &fileContents, &size)) {
        return NULL;
    }

    // parse json
    cJSON *json = cJSON_ParseWithLength(fileContents, size);
    if (!json) { return NULL; }

    free(fileContents); // we no longer need this
    return json;
}

static cJSON *get_json_from_mod(struct Mod *mod) {
    char manifestPath[SYS_MAX_PATH] = { 0 };
    if (!concat_path(manifestPath, mod->basePath, MOD_MANIFEST_ENTRY_FILE)) {
        LOG_ERROR("Failed to concat path '%s' + '%s'", mod->basePath, MOD_MANIFEST_ENTRY_FILE);
        return NULL;
    }
    return get_json_from_path(manifestPath);
}

static bool path_has_traversal(const char *path) {
    if (!path) { return true; }

    const char *start = path;

    // iterate through path
    while (*start) {
        const char *end = start;
        // look for the next path separator to get the end
        while (end[0] && end[0] != *PATH_SEPARATOR) {
            end++;
        }

        size_t len = (size_t)(end - start);

        // check if we are . or ..
        if ((len == 1 && start[0] == '.') ||
            (len == 2 && start[0] == '.' && start[1] == '.')) {
            return true;
        }

        // set start to end + 1 (if end is null, set start to null)
        start = (end[0]) ? end + 1 : NULL;
    }

    return false;
}

char *mod_manifest_get_entry_file_path(const char *path) {
    cJSON *json = get_json_from_path(path);
    if (!json) { return NULL; }

    // scan for an entry file
    cJSON *entryFileJsonEntry = cJSON_GetObjectItemCaseSensitive(json, "entryFile");
    if (!cJSON_IsString(entryFileJsonEntry) || entryFileJsonEntry->valuestring == NULL) {
        cJSON_Delete(json);
        return NULL;
    }

    char *entryFile = entryFileJsonEntry->valuestring;
    normalize_path(entryFile);

    // if it appears to be manipulating path with . and .. return NULL
    if (path_has_traversal(entryFile)) {
        cJSON_Delete(json);
        return NULL;
    }

    entryFile = strdup(entryFile); // deleting the json file deletes where this is pointing, so dupe it
    if (!entryFile) {
        cJSON_Delete(json);
        return NULL;
    }
    cJSON_Delete(json);

    return entryFile;
}

char **mod_manifest_get_array_of_string(struct Mod *mod, const char *key) {
    cJSON *json = get_json_from_mod(mod);
    if (!json) { return NULL; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsArray(jsonItem)) {
        cJSON_Delete(json);
        return NULL;
    }

    int size = cJSON_GetArraySize(jsonItem);

    // allocate array
    char **array = malloc((size + 1) * sizeof(char *)); // + 1 for null termination
    if (!array) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *element = NULL;
    int index = 0;

    // for each element....
    cJSON_ArrayForEach(element, jsonItem) {
        // if we are not a string, cleanup, otherwise....
        if (!cJSON_IsString(element) || element->valuestring == NULL) {
            for (int i = 0; i < index; i++) {
                free(array[i]);
            }
            free(array);
            cJSON_Delete(json);
            return NULL;
        }

        // set string to valuestring
        array[index] = strdup(element->valuestring);
        if (!array[index]) {
            for (int i = 0; i < index; i++) {
                free(array[i]);
            }
            free(array);
            cJSON_Delete(json);
            return NULL;
        }
        index++;
    }

    // null terminate array
    array[index] = NULL;

    cJSON_Delete(json);
    return array;
}

char *mod_manifest_get_string(struct Mod *mod, const char *key) {
    cJSON *json = get_json_from_mod(mod);
    if (!json) { return NULL; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsString(jsonItem) || jsonItem->valuestring == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    char *valueString = strdup(jsonItem->valuestring);
    if (!valueString) {
        cJSON_Delete(json);
        return NULL;
    }
    cJSON_Delete(json);
    return valueString;
}

bool mod_manifest_get_bool(struct Mod *mod, const char *key, bool defaultValue) {
    cJSON *json = get_json_from_mod(mod);
    if (!json) { return defaultValue; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsBool(jsonItem)) {
        cJSON_Delete(json);
        return defaultValue;
    }
    bool returnValue = cJSON_IsTrue(jsonItem);
    cJSON_Delete(json);
    return returnValue;
}
