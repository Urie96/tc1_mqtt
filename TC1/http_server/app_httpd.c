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
#include "cJSON/cJSON.h"

#define app_httpd_log(M, ...) custom_log("apphttpd", M, ##__VA_ARGS__)

#define HTTPD_HDR_DEFORT (HTTPD_HDR_ADD_SERVER|HTTPD_HDR_ADD_CONN_CLOSE|HTTPD_HDR_ADD_PRAGMA_NO_CACHE)
#define HTTP_RES_500 "HTTP/1.1 500 Internal Server Error\r\n"
static bool is_http_init;
static bool is_handlers_registered;
struct httpd_wsgi_call g_app_handlers[];

static int web_send_wifisetting_page(httpd_request_t *req)
{
  OSStatus err = kNoErr;
  
  err = httpd_send_all_header(req, HTTP_RES_200, wifisetting_len, HTTP_CONTENT_HTML_STR);
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

static int web_save_config(httpd_request_t *req)
{
  OSStatus err;
  char request_body[HTTPD_MAX_MESSAGE];
  cJSON *json = NULL;
  cJSON *ssid;
  cJSON *wifi_password;
  cJSON *mqtt_host;
  cJSON *mqtt_port;
  cJSON *mqtt_username;
  cJSON *mqtt_password;
  mico_Context_t *context = mico_system_context_get();

  if (req->body_nbytes >= HTTPD_MAX_MESSAGE - 2) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"request_too_large\"}");
  }

  err = httpd_get_data(req, request_body, sizeof(request_body) - 1);
  if (err != kNoErr) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
  }

  json = cJSON_Parse(request_body);
  if (json == NULL) {
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_json\"}");
  }

  ssid = cJSON_GetObjectItem(json, "ssid");
  wifi_password = cJSON_GetObjectItem(json, "wifi_password");
  mqtt_host = cJSON_GetObjectItem(json, "mqtt_host");
  mqtt_port = cJSON_GetObjectItem(json, "mqtt_port");
  mqtt_username = cJSON_GetObjectItem(json, "mqtt_username");
  mqtt_password = cJSON_GetObjectItem(json, "mqtt_password");

  if (!json_string_is_valid(ssid, maxSsidLen, true)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_ssid\"}");
  }
  if (!json_string_is_valid(wifi_password, maxKeyLen, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_wifi_password\"}");
  }
  if (!json_string_is_valid(mqtt_host, SETTING_MQTT_STRING_LENGTH_MAX, true)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_host\"}");
  }
  if (mqtt_port == NULL || !cJSON_IsNumber(mqtt_port) || mqtt_port->valuedouble != mqtt_port->valueint ||
      mqtt_port->valueint < 1 || mqtt_port->valueint > 65535) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_port\"}");
  }
  if (!json_string_is_valid(mqtt_username, SETTING_MQTT_STRING_LENGTH_MAX, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_username\"}");
  }
  if (!json_string_is_valid(mqtt_password, SETTING_MQTT_STRING_LENGTH_MAX, false)) {
    cJSON_Delete(json);
    return web_send_json(req, HTTP_RES_400, "{\"success\":false,\"error\":\"invalid_mqtt_password\"}");
  }

  strcpy(context->micoSystemConfig.ssid, ssid->valuestring);
  strcpy(context->micoSystemConfig.key, wifi_password->valuestring);
  strcpy(context->micoSystemConfig.user_key, wifi_password->valuestring);
  context->micoSystemConfig.keyLength = strlen(context->micoSystemConfig.key);
  context->micoSystemConfig.user_keyLength = context->micoSystemConfig.keyLength;
  context->micoSystemConfig.channel = 0;
  memset(context->micoSystemConfig.bssid, 0, sizeof(context->micoSystemConfig.bssid));
  context->micoSystemConfig.security = SECURITY_TYPE_AUTO;
  context->micoSystemConfig.dhcpEnable = true;
  context->micoSystemConfig.configured = allConfigured;

  strcpy(user_config->mqtt_ip, mqtt_host->valuestring);
  user_config->mqtt_port = mqtt_port->valueint;
  strcpy(user_config->mqtt_user, mqtt_username->valuestring);
  strcpy(user_config->mqtt_password, mqtt_password->valuestring);
  user_config->version = USER_CONFIG_VERSION;
  cJSON_Delete(json);

  err = mico_system_context_update(context);
  if (err != kNoErr) {
    app_httpd_log("ERROR: Unable to save configuration: %d", err);
    return web_send_json(req, HTTP_RES_500, "{\"success\":false,\"error\":\"save_failed\"}");
  }

  err = web_send_json(req, HTTP_RES_200, "{\"success\":true}");
  if (err != kNoErr) return err;

  mico_thread_msleep(1000);
  return mico_system_power_perform(context, eState_Software_Reset);
}

struct httpd_wsgi_call g_app_handlers[] = {
  {"/", HTTPD_HDR_DEFORT, 0, web_send_wifisetting_page, NULL, NULL, NULL},
  {"/api/config", HTTPD_HDR_DEFORT, 0, NULL, web_save_config, NULL, NULL},
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
