/**
 ******************************************************************************
 * @file    app_https.c
 * @author  QQ DING
 * @version V1.0.0
 * @date    1-September-2015
 * @brief   The main HTTPD server initialization and wsgi handle.
 ******************************************************************************
 *
 *  The MIT License
 *  Copyright (c) 2016 MXCHIP Inc.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is furnished
 *  to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 *  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ******************************************************************************
 */

#include <httpd.h>
#include <http_parse.h>
#include <http-strings.h>

#include "mico.h"
#include "httpd_priv.h"
#include "app_httpd.h"

#include "main.h"
#include "user_gpio.h"
#include "user_mqtt_client.h"
#include "ota_server/ota_server.h"
#include "cJSON/cJSON.h"

#define app_httpd_log(M, ...) custom_log("apphttpd", M, ##__VA_ARGS__)

#define HTTPD_HDR_DEFORT (HTTPD_HDR_ADD_SERVER|HTTPD_HDR_ADD_CONN_CLOSE|HTTPD_HDR_ADD_PRAGMA_NO_CACHE)
#define HTTP_RES_500 "HTTP/1.1 500 Internal Server Error\r\n"
static bool is_http_init;
static bool is_handlers_registered;
struct httpd_wsgi_call g_app_handlers[];

/* ── HTTP body reader: loops until Content-Length bytes received ── */
static int httpd_read_body(httpd_request_t *req, char *buffer, size_t bufsize)
{
  int ret;
  size_t expected;
  size_t total_read = 0;

  /* Parse headers if not yet done */
  if (!req->hdr_parsed) {
    char *hdr_buf = malloc(HTTPD_MAX_MESSAGE);
    if (!hdr_buf) return kGeneralErr;
    ret = httpd_parse_hdr_tags(req, req->sock, hdr_buf, HTTPD_MAX_MESSAGE);
    free(hdr_buf);
    if (ret != kNoErr) return kGeneralErr;
    req->hdr_parsed = 1;
  }

  expected = req->body_nbytes;
  if (expected == 0) {
    buffer[0] = '\0';
    return kNoErr;
  }
  /* Need room for all body data plus null terminator */
  if (expected >= bufsize) return kGeneralErr;

  while (total_read < expected) {
    ret = httpd_recv(req->sock, buffer + total_read, expected - total_read, 0);
    if (ret <= 0) return kGeneralErr;
    total_read += ret;
  }

  buffer[total_read] = '\0';
  return kNoErr;
}

static int web_send_wifisetting_page(httpd_request_t *req)
{
  OSStatus err = kNoErr;
  
  err = httpd_send_all_header(req, HTTP_RES_200, wifisetting_len, "text/html; charset=utf-8");
  require_noerr_action( err, exit, app_httpd_log("ERROR: Unable to send http wifisetting headers.") );
  
  err = httpd_send_body(req->sock, wifisetting, wifisetting_len);
  require_noerr_action( err, exit, app_httpd_log("ERROR: Unable to send http wifisetting body.") );
  
exit:
  return err; 
}

static int web_send_json(httpd_request_t *req, const char *status, const char *body)
{
  OSStatus err = kNoErr;
  int body_len = strlen(body);

  err = httpd_send_all_header(req, status, body_len, HTTP_CONTENT_JSON_STR);
  require_noerr_action(err, exit, app_httpd_log("ERROR: Unable to send JSON headers."));

  err = httpd_send_body(req->sock, (const unsigned char *) body, body_len);
  require_noerr_action(err, exit, app_httpd_log("ERROR: Unable to send JSON body."));

exit:
  return err;
}

static bool json_string_is_valid(cJSON *item, size_t max_length, bool required)
{
  size_t length;

  if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL) return false;
  length = strlen(item->valuestring);
  return length < max_length && (!required || length > 0);
}

static int web_get_config(httpd_request_t *req)
{
  OSStatus err;
  cJSON *root = NULL;
  char *json_str = NULL;
  char power_str[16];
  int i;
  IPStatusTypedef para;

  root = cJSON_CreateObject();
  if (root == NULL) {
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"internal_error\"}");
  }

  cJSON_AddStringToObject(root, "name", sys_config->micoSystemConfig.name);
  cJSON_AddStringToObject(root, "version", VERSION);
  cJSON_AddStringToObject(root, "mac", strMac);

  memset(&para, 0, sizeof(para));
  if (micoWlanGetIPStatus(&para, Station) != kNoErr) {
    /* Station not connected – try SoftAP */
    memset(&para, 0, sizeof(para));
    micoWlanGetIPStatus(&para, Soft_AP);
  }
  cJSON_AddStringToObject(root, "ip", (para.ip[0] != '\0') ? para.ip : "0.0.0.0");
  cJSON_AddStringToObject(root, "ssid", (const char *)sys_config->micoSystemConfig.ssid);

  cJSON_AddStringToObject(root, "mqtt_ip", user_config->mqtt_ip);
  cJSON_AddNumberToObject(root, "mqtt_port", user_config->mqtt_port);
  cJSON_AddStringToObject(root, "mqtt_user", user_config->mqtt_user);

  /* Return boolean instead of plaintext password */
  cJSON_AddBoolToObject(root, "mqtt_password_set", user_config->mqtt_password[0] != '\0');
  cJSON_AddBoolToObject(root, "wifi_password_set", sys_config->micoSystemConfig.user_key[0] != '\0');
  cJSON_AddStringToObject(root, "ota_url", user_config->ota_url);
  cJSON_AddStringToObject(root, "ota_md5", user_config->ota_md5);

  for (i = 0; i < SLOT_NUM; i++) {
    char key[8];
    snprintf(key, sizeof(key), "slot%d", i);
    cJSON_AddNumberToObject(root, key, user_config->slot[i]);
  }

  snprintf(power_str, sizeof(power_str), "%ld.%ld", (long)(power / 10), (long)(power % 10));
  cJSON_AddStringToObject(root, "power", power_str);

  json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (json_str == NULL) {
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"internal_error\"}");
  }

  err = web_send_json(req, HTTP_RES_200, json_str);
  free(json_str);
  return err;
}

static int web_update_config(httpd_request_t *req)
{
  OSStatus err;
  char request_body[HTTPD_MAX_MESSAGE];
  cJSON *json = NULL;
  cJSON *item;
  bool changed = false;
  mico_Context_t *context = mico_system_context_get();
  mico_sys_config_t *orig_sys = NULL;
  user_config_t *orig_user = NULL;

  err = httpd_read_body(req, request_body, sizeof(request_body));
  if (err != kNoErr) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
  }

  json = cJSON_Parse(request_body);
  if (json == NULL) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
  }

  /* ═══ Phase 1: Validate all fields before modifying anything ═══ */

  item = cJSON_GetObjectItem(json, "ssid");
  if (item != NULL && !json_string_is_valid(item, maxSsidLen, true)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_ssid\"}");
  }

  item = cJSON_GetObjectItem(json, "wifi_password");
  if (item != NULL && !json_string_is_valid(item, maxKeyLen, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_wifi_password\"}");
  }

  item = cJSON_GetObjectItem(json, "mqtt_host");
  if (item != NULL && !json_string_is_valid(item, SETTING_MQTT_STRING_LENGTH_MAX, true)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_host\"}");
  }

  item = cJSON_GetObjectItem(json, "mqtt_port");
  if (item != NULL) {
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint ||
        item->valueint < 1 || item->valueint > 65535) {
      cJSON_Delete(json);
      return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_port\"}");
    }
  }

  item = cJSON_GetObjectItem(json, "mqtt_username");
  if (item != NULL && !json_string_is_valid(item, SETTING_MQTT_STRING_LENGTH_MAX, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_username\"}");
  }

  item = cJSON_GetObjectItem(json, "mqtt_password");
  if (item != NULL && !json_string_is_valid(item, SETTING_MQTT_STRING_LENGTH_MAX, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_password\"}");
  }

  item = cJSON_GetObjectItem(json, "name");
  if (item != NULL && !json_string_is_valid(item, maxSsidLen, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_name\"}");
  }

  /* ═══ Phase 2: Snapshot current state ═══ */

  orig_sys = (mico_sys_config_t *)malloc(sizeof(mico_sys_config_t));
  orig_user = (user_config_t *)malloc(sizeof(user_config_t));
  if (!orig_sys || !orig_user) {
    free(orig_sys);
    free(orig_user);
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"internal_error\"}");
  }
  memcpy(orig_sys, &context->micoSystemConfig, sizeof(mico_sys_config_t));
  memcpy(orig_user, user_config, sizeof(user_config_t));

  /* ═══ Phase 3: Apply changes ═══ */

  item = cJSON_GetObjectItem(json, "ssid");
  if (item != NULL) {
    strcpy(context->micoSystemConfig.ssid, item->valuestring);
    changed = true;
  }

  item = cJSON_GetObjectItem(json, "wifi_password");
  if (item != NULL) {
    strcpy(context->micoSystemConfig.key, item->valuestring);
    strcpy(context->micoSystemConfig.user_key, item->valuestring);
    context->micoSystemConfig.keyLength = strlen(context->micoSystemConfig.key);
    context->micoSystemConfig.user_keyLength = context->micoSystemConfig.keyLength;
    context->micoSystemConfig.channel = 0;
    memset(context->micoSystemConfig.bssid, 0, sizeof(context->micoSystemConfig.bssid));
    context->micoSystemConfig.security = SECURITY_TYPE_AUTO;
    context->micoSystemConfig.dhcpEnable = true;
    context->micoSystemConfig.configured = allConfigured;
    changed = true;
  }

  item = cJSON_GetObjectItem(json, "mqtt_host");
  if (item != NULL) {
    strcpy(user_config->mqtt_ip, item->valuestring);
    changed = true;
  }

  item = cJSON_GetObjectItem(json, "mqtt_port");
  if (item != NULL) {
    user_config->mqtt_port = item->valueint;
    changed = true;
  }

  item = cJSON_GetObjectItem(json, "mqtt_username");
  if (item != NULL) {
    strcpy(user_config->mqtt_user, item->valuestring);
    changed = true;
  }

  item = cJSON_GetObjectItem(json, "mqtt_password");
  if (item != NULL) {
    strcpy(user_config->mqtt_password, item->valuestring);
    changed = true;
  }

  item = cJSON_GetObjectItem(json, "name");
  if (item != NULL) {
    snprintf(sys_config->micoSystemConfig.name, sizeof(sys_config->micoSystemConfig.name),
             "%s", item->valuestring);
    changed = true;
  }

  /* OTA URL (optional) */
  item = cJSON_GetObjectItem(json, "ota_url");
  if (item != NULL) {
    if (!json_string_is_valid(item, OTA_URL_LENGTH_MAX, false)) {
      cJSON_Delete(json);
      return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_ota_url\"}");
    }
    strcpy(user_config->ota_url, item->valuestring);
    changed = true;
  }

  /* OTA MD5 (optional) */
  item = cJSON_GetObjectItem(json, "ota_md5");
  if (item != NULL) {
    if (!json_string_is_valid(item, OTA_MD5_LENGTH_MAX, false)) {
      cJSON_Delete(json);
      return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_ota_md5\"}");
    }
    strcpy(user_config->ota_md5, item->valuestring);
    changed = true;
  }

  cJSON_Delete(json);

  /* ═══ Phase 4: Persist ═══ */

  if (changed) {
    user_config->version = USER_CONFIG_VERSION;
    err = mico_system_context_update(context);
    if (err != kNoErr) {
      /* Rollback RAM to snapshot */
      memcpy(&context->micoSystemConfig, orig_sys, sizeof(mico_sys_config_t));
      memcpy(user_config, orig_user, sizeof(user_config_t));
      free(orig_sys);
      free(orig_user);
      app_httpd_log("ERROR: Unable to save configuration: %d", err);
      return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"save_failed\"}");
    }
  }

  free(orig_sys);
  free(orig_user);
  return web_send_json(req, HTTP_RES_200, "{\"success\":true}");
}

static int web_reboot(httpd_request_t *req)
{
  OSStatus err;
  mico_Context_t *context = mico_system_context_get();

  err = web_send_json(req, HTTP_RES_200, "{\"success\":true}");
  if (err != kNoErr) return err;

  mico_thread_msleep(1000);
  return mico_system_power_perform(context, eState_Software_Reset);
}

static int web_get_relay(httpd_request_t *req)
{
  OSStatus err;
  cJSON *root = NULL;
  char *json_str = NULL;
  int i;

  root = cJSON_CreateObject();
  if (root == NULL) {
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"internal_error\"}");
  }

  for (i = 0; i < SLOT_NUM; i++) {
    char key[8];
    snprintf(key, sizeof(key), "slot%d", i);
    cJSON_AddNumberToObject(root, key, user_config->slot[i]);
  }

  json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (json_str == NULL) {
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"internal_error\"}");
  }

  err = web_send_json(req, HTTP_RES_200, json_str);
  free(json_str);
  return err;
}

static int web_set_relay(httpd_request_t *req)
{
  OSStatus err;
  char request_body[HTTPD_MAX_MESSAGE];
  cJSON *json = NULL;
  cJSON *item;
  int i;
  bool changed = false;
  uint8_t original_slots[SLOT_NUM];
  int changed_slots[SLOT_NUM];   /* index of slots actually changed */
  int changed_count = 0;

  err = httpd_read_body(req, request_body, sizeof(request_body));
  if (err != kNoErr) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
  }

  json = cJSON_Parse(request_body);
  if (json == NULL) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
  }

  /* ═══ Phase 1: Validate all values before touching hardware ═══ */
  for (i = 0; i < SLOT_NUM; i++) {
    char key[8];
    snprintf(key, sizeof(key), "slot%d", i);
    item = cJSON_GetObjectItem(json, key);
    if (item != NULL) {
      if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint ||
          (item->valueint != 0 && item->valueint != 1)) {
        cJSON_Delete(json);
        return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_value\"}");
      }
    }
  }

  /* ═══ Phase 2: Snapshot original slot states ═══ */
  memcpy(original_slots, user_config->slot, SLOT_NUM);

  /* ═══ Phase 3: Apply GPIO + MQTT report ═══ */
  for (i = 0; i < SLOT_NUM; i++) {
    char key[8];
    snprintf(key, sizeof(key), "slot%d", i);
    item = cJSON_GetObjectItem(json, key);
    if (item != NULL) {
      int state = item->valueint;
      user_relay_set(i, state);
      user_mqtt_send_slot_state(i);
      changed_slots[changed_count++] = i;
      changed = true;
    }
  }

  cJSON_Delete(json);

  /* ═══ Phase 4: Persist; rollback on failure ═══ */
  if (changed) {
    err = mico_system_context_update(mico_system_context_get());
    if (err != kNoErr) {
      /* Rollback GPIO and RAM for every slot we changed */
      int j;
      for (j = 0; j < changed_count; j++) {
        int idx = changed_slots[j];
        user_relay_set(idx, original_slots[idx]);
        user_mqtt_send_slot_state(idx);
      }
      app_httpd_log("ERROR: Unable to save relay state: %d", err);
      return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"save_failed\"}");
    }
  }

  /* Return updated states */
  return web_get_relay(req);
}

static int web_ota_start(httpd_request_t *req)
{
  OSStatus err;
  char request_body[HTTPD_MAX_MESSAGE];
  cJSON *json = NULL;
  cJSON *item;
  char *url = NULL;
  char *md5 = NULL;

  /* Check if OTA already running */
  if (ota_server_state_get() != OTA_CONTROL_IDLE) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"ota_in_progress\"}");
  }

  /* Parse optional body for url/md5 override */
  err = httpd_read_body(req, request_body, sizeof(request_body));
  if (err == kNoErr && request_body[0] != '\0') {
    json = cJSON_Parse(request_body);
    if (json == NULL) {
      return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
    }
    item = cJSON_GetObjectItem(json, "url");
    if (item != NULL && cJSON_IsString(item) && item->valuestring != NULL) {
      url = item->valuestring;
    }
    item = cJSON_GetObjectItem(json, "md5");
    if (item != NULL && cJSON_IsString(item) && item->valuestring != NULL) {
      md5 = item->valuestring;
    }
  }

  /* Fall back to saved config */
  if (url == NULL) {
    url = user_config->ota_url;
  }
  if (md5 == NULL) {
    if (user_config->ota_md5[0] != '\0') {
      md5 = user_config->ota_md5;
    }
  }

  if (url == NULL || url[0] == '\0') {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"no_ota_url\"}");
  }

  app_httpd_log("Starting OTA: %s", url);
  err = ota_server_start(url, md5, NULL);
  cJSON_Delete(json);
  if (err != kNoErr) {
    app_httpd_log("ERROR: ota_server_start failed: %d", err);
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"ota_start_failed\"}");
  }

  return web_send_json(req, HTTP_RES_200, "{\"success\":true}");
}

struct httpd_wsgi_call g_app_handlers[] = {
  {"/", HTTPD_HDR_DEFORT, 0, web_send_wifisetting_page, NULL, NULL, NULL},
  {"/api/config", HTTPD_HDR_DEFORT, 0, web_get_config, NULL, web_update_config, NULL},
  {"/api/relay", HTTPD_HDR_DEFORT, 0, web_get_relay, web_set_relay, NULL, NULL},
  {"/api/reboot", HTTPD_HDR_DEFORT, 0, NULL, web_reboot, NULL, NULL},
  {"/api/ota", HTTPD_HDR_DEFORT, 0, NULL, web_ota_start, NULL, NULL},
  {"/setting.htm", HTTPD_HDR_DEFORT, 0, web_send_wifisetting_page, NULL, NULL, NULL},
};

static int g_app_handlers_no = sizeof(g_app_handlers)/sizeof(struct httpd_wsgi_call);

static void app_http_register_handlers()
{
  int rc;
  rc = httpd_register_wsgi_handlers(g_app_handlers, g_app_handlers_no);
  if (rc) {
    app_httpd_log("failed to register test web handler");
  }
}

static int _app_httpd_start()
{
  OSStatus err = kNoErr;
  app_httpd_log("initializing web-services");
  
  /*Initialize HTTPD*/
  if(is_http_init == false) {
    err = httpd_init();
    require_noerr_action( err, exit, app_httpd_log("failed to initialize httpd") );
    is_http_init = true;
  }
  
  /*Start http thread*/
  err = httpd_start();
  if(err != kNoErr) {
    app_httpd_log("failed to start httpd thread");
    httpd_shutdown();
  }
exit:
  return err;
}

int app_httpd_start( void )
{
  OSStatus err = kNoErr;
  
  err = _app_httpd_start();
  require_noerr( err, exit ); 
  
  if (is_handlers_registered == false) {
    app_http_register_handlers();
    is_handlers_registered = true;
  }
  
exit:
  return err;
}

int app_httpd_stop()
{
  OSStatus err = kNoErr;
  
  /* HTTPD and services */
  app_httpd_log("stopping down httpd");
  err = httpd_stop();
  require_noerr_action( err, exit, app_httpd_log("failed to halt httpd") );
  
exit:
  return err;
}
