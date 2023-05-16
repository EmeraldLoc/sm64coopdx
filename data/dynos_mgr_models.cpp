#include <map>
#include <vector>
#include "dynos.cpp.h"

extern "C" {
#include "engine/geo_layout.h"
#include "engine/graph_node.h"
#include "model_ids.h"
}

enum ModelLoadType {
    MLT_GEO,
    MLT_DL,
    MLT_STORE,
};

struct ModelInfo {
    u32 id;
    void* asset;
    struct GraphNode* graphNode;
    enum ModelPool modelPool;
};

struct ScheduledFreePool {
    struct DynamicPool* pool;
    u32 timeout;
};

static struct DynamicPool* sModelPools[MODEL_POOL_MAX] = { 0 };

static std::map<void*, struct ModelInfo> sAssetMap[MODEL_POOL_MAX];
static std::map<u32, std::vector<struct ModelInfo>> sIdMap;
static std::map<u32, u32> sOverwriteMap;

static std::vector<struct ScheduledFreePool> sPoolsToFree;

static u32 find_empty_id() {
    u32 id = 256;
    while (true) {
        if (sIdMap.count(id) == 0)  { return id; }
        if (sIdMap[id].size() == 0) { return id; }
        id++;
    }
}

void DynOS_Model_Dump() {
    for (auto& it : sIdMap) {
        if (it.second.size() == 0 || it.second.empty()) { continue; }
        printf(">> [%03x] ", it.first);
        for (auto& it2 : it.second) {
            switch (it2.modelPool) {
                case MODEL_POOL_PERMANENT: printf("P "); break;
                case MODEL_POOL_SESSION:   printf("S "); break;
                case MODEL_POOL_LEVEL:     printf("L "); break;
                case MODEL_POOL_MAX:       printf("M "); break;
            }
            printf("%p ", it2.graphNode);
        }
        printf("\n");
    }
}

struct GraphNode* DynOS_Model_LoadCommon(u32* aId, enum ModelPool aModelPool, void* aAsset, u8 aLayer, struct GraphNode* aGraphNode, enum ModelLoadType mlt) {
    // sanity check pool
    if (aModelPool >= MODEL_POOL_MAX) { return NULL; }

    // allocate pool
    if (!sModelPools[aModelPool]) {
        sModelPools[aModelPool] = dynamic_pool_init();
    }

    // check map
    auto& map = sAssetMap[aModelPool];
    if (map.count(aAsset)) {
        auto& found = map[aAsset];
        if (*aId && *aId != found.id) {
            sOverwriteMap[*aId] = found.id;
        }
        *aId = found.id;
        return found.graphNode;
    }

    // load geo
    struct GraphNode* node = NULL;
    switch (mlt) {
        case MLT_GEO:
            node = process_geo_layout(sModelPools[aModelPool], aAsset);
            break;
        case MLT_DL:
            node = (struct GraphNode *) init_graph_node_display_list(sModelPools[aModelPool], NULL, aLayer, aAsset);
            break;
        case MLT_STORE:
            node = aGraphNode;
            break;
    }
    if (!node) { return NULL; }

    // figure out id
    if (!*aId) { *aId = find_empty_id(); }

    // create model info
    struct ModelInfo info = {
        .id = *aId,
        .asset = aAsset,
        .graphNode = node,
        .modelPool = aModelPool
    };

    // store in maps
    sIdMap[*aId].push_back(info);
    map[aAsset] = info;

    return node;
}

struct GraphNode* DynOS_Model_LoadGeo(u32* aId, enum ModelPool aModelPool, void* aAsset) {
    return DynOS_Model_LoadCommon(aId, aModelPool, aAsset, 0, NULL, MLT_GEO);
}

struct GraphNode* DynOS_Model_LoadDl(u32* aId, enum ModelPool aModelPool, u8 aLayer, void* aAsset) {
    return DynOS_Model_LoadCommon(aId, aModelPool, aAsset, aLayer, NULL, MLT_DL);
}

struct GraphNode* DynOS_Model_StoreGeo(u32* aId, enum ModelPool aModelPool, void* aAsset, struct GraphNode* aGraphNode) {
    return DynOS_Model_LoadCommon(aId, aModelPool, aAsset, 0, aGraphNode, MLT_STORE);
}

struct GraphNode* DynOS_Model_GetErrorGeo() {
    if (!sIdMap.count(MODEL_ERROR_MODEL)) { return NULL; }
    auto& vec = sIdMap[MODEL_ERROR_MODEL];
    if (vec.size() == 0 || vec.empty()) {
        return NULL;
    }
    return vec.back().graphNode;
}

struct GraphNode* DynOS_Model_GetGeo(u32 aId) {
    if (!aId) { return NULL; }

    if (sOverwriteMap.count(aId)) { aId = sOverwriteMap[aId]; }

    if (sIdMap.count(aId) == 0) {
        return DynOS_Model_GetErrorGeo();
    }

    auto& vec = sIdMap[aId];
    if (vec.size() == 0 || vec.empty()) {
        return DynOS_Model_GetErrorGeo();
    }

    return vec.back().graphNode;
}

u32 DynOS_Model_GetIdFromAsset(void* asset) {
    if (!asset) { return MODEL_NONE; }
    for (int i = 0; i < MODEL_POOL_MAX; i++) {
        if (sAssetMap[i].count(asset)) {
            return sAssetMap[i][asset].id;
        }
    }
    return MODEL_ERROR_MODEL;
}

void DynOS_Model_OverwriteSlot(u32 srcSlot, u32 dstSlot) {
    sOverwriteMap[srcSlot] = dstSlot;
}

void DynOS_Model_ClearPool(enum ModelPool aModelPool) {
    if (!sModelPools[aModelPool]) { return; }

    // schedule pool to be freed
    sPoolsToFree.push_back({
        .pool = sModelPools[aModelPool],
        .timeout = 30
    });

    // clear pointer
    sModelPools[aModelPool] = NULL;

    // clear overwrite
    if (aModelPool == MODEL_POOL_LEVEL) {
        sOverwriteMap.clear();
    }

    // clear maps
    auto& assetMap = sAssetMap[aModelPool];
    for (auto& asset : assetMap) {
        auto& info = asset.second;
        if (sIdMap.count(info.id) == 0) { continue; }

        // erase from id map
        auto& idMap = sIdMap[info.id];
        for (auto info2 = idMap.begin(); info2 != idMap.end(); ) {
            if (info.id == info2->id && info2->modelPool == aModelPool) {
                info2 = idMap.erase(info2);
            } else {
                info2++;
            }
        }
    }

    assetMap.clear();
}

void DynOS_Model_Update() {
    for (auto it = sPoolsToFree.begin(); it != sPoolsToFree.end(); ) {
        if (--it->timeout <= 0) {
            dynamic_pool_free_pool(it->pool);
            it = sPoolsToFree.erase(it);
        } else {
            it++;
        }
    }
}