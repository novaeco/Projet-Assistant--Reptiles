#include "json_proxy.h"
#include "cJSON.h"

void json_proxy_init(void) {
    // Dummy function to ensure linkage
    cJSON *root = cJSON_CreateObject();
    if (root) cJSON_Delete(root);
}