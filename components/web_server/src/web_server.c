#include "web_server.h"
#include "core_service.h"
#include "reptile_storage.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// =============================================================================
// HTML Content
// =============================================================================

static const char *INDEX_HTML = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>Reptile Manager</title>"
"<style>"
"body { font-family: sans-serif; margin: 20px; }"
"table { width: 100%; border-collapse: collapse; margin-top: 20px; }"
"th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
"th { background-color: #f2f2f2; }"
".btn { padding: 10px 15px; background: #007bff; color: white; border: none; cursor: pointer; }"
".btn:hover { background: #0056b3; }"
"input { padding: 8px; margin: 5px 0; width: 100%; box-sizing: border-box; }"
"</style>"
"</head>"
"<body>"
"<h1>Reptile Manager</h1>"
"<h3>Ajouter un Animal</h3>"
"<form id='addForm'>"
"<input type='text' id='name' placeholder='Nom' required>"
"<input type='text' id='species' placeholder='Espece' required>"
"<button type='submit' class='btn'>Ajouter</button>"
"</form>"
"<h3>Liste des Animaux</h3>"
"<button onclick='loadAnimals()' class='btn'>Rafraichir</button>"
"<table id='animalTable'>"
"<thead><tr><th>Nom</th><th>Espece</th><th>ID</th></tr></thead>"
"<tbody></tbody>"
"</table>"
"<script>"
"document.getElementById('addForm').addEventListener('submit', function(e) {"
"  e.preventDefault();"
"  const data = {"
"    name: document.getElementById('name').value,"
"    species: document.getElementById('species').value"
"  };"
"  fetch('/api/animals', {"
"    method: 'POST',"
"    headers: {'Content-Type': 'application/json'},"
"    body: JSON.stringify(data)"
"  }).then(res => {"
"    if(res.ok) { loadAnimals(); document.getElementById('addForm').reset(); }"
"    else alert('Erreur ajout');"
"  });"
"});"
"function loadAnimals() {"
"  fetch('/api/animals').then(res => res.json()).then(data => {"
"    const tbody = document.querySelector('#animalTable tbody');"
"    tbody.innerHTML = '';"
"    data.forEach(a => {"
"      const tr = document.createElement('tr');"
"      tr.innerHTML = `<td>${a.name}</td><td>${a.species}</td><td>${a.id}</td>`;"
"      tbody.appendChild(tr);"
"    });"
"  });"
"}"
"loadAnimals();"
"</script>"
"</body>"
"</html>";

// =============================================================================
// Handlers
// =============================================================================

/* GET / handler */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* GET /api/animals handler */
static esp_err_t api_animals_get_handler(httpd_req_t *req)
{
    animal_summary_t *list = NULL;
    size_t count = 0;
    
    // Use core_list_animals (which uses core_search_animals internally now)
    if (core_list_animals(&list, &count) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", list[i].id);
        cJSON_AddStringToObject(item, "name", list[i].name);
        cJSON_AddStringToObject(item, "species", list[i].species);
        cJSON_AddItemToArray(root, item);
    }
    core_free_animal_list(list);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free((void*)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /api/animals handler */
static esp_err_t api_animals_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *name = cJSON_GetObjectItem(root, "name");
    cJSON *species = cJSON_GetObjectItem(root, "species");

    if (name && species) {
        animal_t new_animal;
        memset(&new_animal, 0, sizeof(animal_t));
        
        // Generate simple ID (UUID-like would be better, but random hex for now)
        snprintf(new_animal.id, sizeof(new_animal.id), "%08lx-%04x", (unsigned long)rand(), rand() & 0xFFFF);
        
        strncpy(new_animal.name, name->valuestring, sizeof(new_animal.name)-1);
        strncpy(new_animal.species, species->valuestring, sizeof(new_animal.species)-1);
        new_animal.dob = 0; // Default
        new_animal.sex = SEX_UNKNOWN;

        if (core_save_animal(&new_animal) == ESP_OK) {
            httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
        } else {
            httpd_resp_send_500(req);
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name or species");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// =============================================================================
// Init
// =============================================================================

esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // Increase stack for JSON processing

    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        // URI: /
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        // URI: /api/animals (GET)
        httpd_uri_t animals_get_uri = {
            .uri       = "/api/animals",
            .method    = HTTP_GET,
            .handler   = api_animals_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &animals_get_uri);

        // URI: /api/animals (POST)
        httpd_uri_t animals_post_uri = {
            .uri       = "/api/animals",
            .method    = HTTP_POST,
            .handler   = api_animals_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &animals_post_uri);

        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return ESP_FAIL;
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}