#include "net_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "core_service.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>

static const char *TAG = "NET_SERVER";
static httpd_handle_t server = NULL;

// =============================================================================
// Handlers
// =============================================================================

// GET /api/animals
static esp_err_t api_animals_handler(httpd_req_t *req)
{
    animal_summary_t *animals = NULL;
    size_t count = 0;
    
    if (core_list_animals(&animals, &count) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", animals[i].id);
        cJSON_AddStringToObject(item, "name", animals[i].name);
        cJSON_AddStringToObject(item, "species", animals[i].species);
        cJSON_AddItemToArray(root, item);
    }
    core_free_animal_list(animals);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    
    cJSON_Delete(root);
    free((void*)json_str);
    return ESP_OK;
}

// GET /reports
static esp_err_t reports_list_handler(httpd_req_t *req)
{
    char **reports = NULL;
    size_t count = 0;
    
    if (core_list_reports(&reports, &count) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Simple HTML list
    httpd_resp_sendstr_chunk(req, "<html><head><title>Rapports</title></head><body><h1>Rapports Disponibles</h1><ul>");
    
    for (size_t i = 0; i < count; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "<li><a href=\"/reports/%s\">%s</a></li>", reports[i], reports[i]);
        httpd_resp_sendstr_chunk(req, buf);
    }
    
    httpd_resp_sendstr_chunk(req, "</ul></body></html>");
    httpd_resp_sendstr_chunk(req, NULL); // Finish

    core_free_report_list(reports, count);
    return ESP_OK;
}

// GET /reports/*
static esp_err_t report_download_handler(httpd_req_t *req)
{
    char filepath[256];
    // Skip "/reports/" prefix (length 9)
    snprintf(filepath, sizeof(filepath), "/sdcard/reports/%s", req->uri + 9);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char *chunk = malloc(1024);
    size_t chunksize;
    while ((chunksize = fread(chunk, 1, 1024, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(f);
            free(chunk);
            return ESP_FAIL;
        }
    }
    free(chunk);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// =============================================================================
// Server Control
// =============================================================================

esp_err_t net_server_start(void)
{
    if (server) return ESP_OK; // Already started

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t api_animals = {
            .uri       = "/api/animals",
            .method    = HTTP_GET,
            .handler   = api_animals_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_animals);

        httpd_uri_t reports_list = {
            .uri       = "/reports",
            .method    = HTTP_GET,
            .handler   = reports_list_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &reports_list);

        httpd_uri_t report_download = {
            .uri       = "/reports/*",
            .method    = HTTP_GET,
            .handler   = report_download_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &report_download);

        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return ESP_FAIL;
}

esp_err_t net_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    return ESP_OK;
}