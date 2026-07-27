#include "user_wifi.h"

#include "main.h"
#include "mico_socket.h"
#include "user_gpio.h"

#include "user_function.h"
#include "http_server/app_httpd.h"
#define os_log(format, ...)  custom_log("WIFI", format, ##__VA_ARGS__)

char wifi_status = WIFI_STATE_NOCONNECT;

mico_timer_t wifi_led_timer;
static bool softap_started = false;

static void wifi_connect_sys_config( void )
{
    if ( strlen( sys_config->micoSystemConfig.ssid ) > 0 )
    {
        os_log("connect ssid:%s key:%s",sys_config->micoSystemConfig.ssid,sys_config->micoSystemConfig.user_key);
        network_InitTypeDef_st wNetConfig;
        memset( &wNetConfig, 0, sizeof(network_InitTypeDef_st) );
        strcpy( wNetConfig.wifi_ssid, sys_config->micoSystemConfig.ssid );
        strcpy( wNetConfig.wifi_key, sys_config->micoSystemConfig.user_key );
        wNetConfig.wifi_mode = Station;
        wNetConfig.dhcpMode = DHCP_Client;
        wNetConfig.wifi_retry_interval = 6000;
        micoWlanStart( &wNetConfig );
        wifi_status = WIFI_STATE_CONNECTING;
    } else {
        os_log("no wifi config saved, start softap");
        wifi_start_softap( );
    }
}
static void get_mac_str( char *mac_str )
{
    uint8_t mac[6];
    mico_wlan_get_mac_address( mac );
    sprintf( mac_str, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
}

void wifi_start_softap( )
{
    OSStatus err;
    network_InitTypeDef_st wNetConfig;
    char mac_str[13];

    if ( softap_started )
    {
        wifi_status = WIFI_STATE_SOFTAP;
        return;
    }

    micoWlanSuspendStation( );
    get_mac_str( mac_str );

    memset( &wNetConfig, 0, sizeof(network_InitTypeDef_st) );
    wNetConfig.wifi_mode = Soft_AP;
    // SSID: TC1_ + 后6位MAC地址
    snprintf( wNetConfig.wifi_ssid, 32, "TC1_%s", mac_str + 6 );
    strcpy( (char*) wNetConfig.wifi_key, "" );          // 无密码
    strcpy( (char*) wNetConfig.local_ip_addr, "10.10.10.1" );
    strcpy( (char*) wNetConfig.net_mask, "255.255.255.0" );
    strcpy( (char*) wNetConfig.gateway_ip_addr, "10.10.10.1" );
    strcpy( (char*) wNetConfig.dnsServer_ip_addr, "10.10.10.1" );
    wNetConfig.dhcpMode = DHCP_Server;
    err = micoWlanStart( &wNetConfig );
    if ( err != kNoErr )
    {
        os_log("SoftAP start failed: %d", err);
        wifi_status = WIFI_STATE_FAIL;
        return;
    }

    wifi_status = WIFI_STATE_SOFTAP;
    softap_started = true;
    os_log("SoftAP started: %s, IP: 10.10.10.1", wNetConfig.wifi_ssid);

    user_led_set( 1 );
}

void wifi_stop_softap( )
{
    if ( softap_started )
    {
        micoWlanSuspendSoftAP( );
        softap_started = false;
    }
    wifi_status = WIFI_STATE_NOCONNECT;
    os_log("SoftAP stopped");
}

// SoftAP 配置完成回调（由 app_httpd 保存配置后调用）
void wifi_softap_configured( void )
{
    os_log("SoftAP config received, connecting to WiFi...");
    wifi_status = WIFI_STATE_SOFTAP_CONFIGURED;
    wifi_stop_softap( );
    wifi_connect_sys_config( );
}

//wifi已连接获取到IP地址 回调
static void wifi_get_ip_callback( IPStatusTypedef *pnet, void * arg )
{
    os_log("got IP:%s", pnet->ip);
    wifi_status = WIFI_STATE_CONNECTED;
}
//wifi连接失败回调
static void wifi_connect_failed_callback( OSStatus err, void *arg )
{
    os_log("wifi connect failed:%d", err);
    if ( wifi_status == WIFI_STATE_SOFTAP || wifi_status == WIFI_STATE_SOFTAP_REQUESTED ) return;
    wifi_status = WIFI_STATE_FAIL;
    if ( !mico_rtos_is_timer_running( &wifi_led_timer ) ) mico_rtos_start_timer( &wifi_led_timer );
}
//wifi连接状态改变回调
static void wifi_status_callback( WiFiEvent status, void *arg )
{
    if ( status == NOTIFY_STATION_UP ) //wifi连接成功
    {
        //wifi_status = WIFI_STATE_CONNECTED;
    } else if ( status == NOTIFY_STATION_DOWN ) //wifi断开
    {
        if ( wifi_status == WIFI_STATE_SOFTAP || wifi_status == WIFI_STATE_SOFTAP_REQUESTED ) return;
        wifi_status = WIFI_STATE_NOCONNECT;
        if ( !mico_rtos_is_timer_running( &wifi_led_timer ) ) mico_rtos_start_timer( &wifi_led_timer );
    }
}
//100ms定时器回调
static void wifi_led_timer_callback( void* arg )
{
    static unsigned int num = 0;
    num++;

    switch ( wifi_status )
    {
        case WIFI_STATE_FAIL:
            os_log("wifi connect fail, start softap");
            wifi_start_softap( );
            break;
        case WIFI_STATE_NOCONNECT:
            wifi_connect_sys_config( );
            break;

        case WIFI_STATE_CONNECTING:
            //if ( num > 1 )
        {
            num = 0;
            user_led_set( -1 );
        }
            break;
        case WIFI_STATE_SOFTAP_REQUESTED:
            wifi_start_softap( );
            break;
        case WIFI_STATE_SOFTAP:
            // LED 慢闪：1s 亮 / 1s 灭
            user_led_set( num % 2 );
            break;
        case WIFI_STATE_SOFTAP_CONFIGURED:
            os_log("softap configured, connecting to WiFi...");
            user_led_set( 1 );
            // 切换到 Station 模式连接 WiFi
            wifi_status = WIFI_STATE_CONNECTING;
            break;
        case WIFI_STATE_CONNECTED:
            user_led_set( 0 );
            mico_rtos_stop_timer( &wifi_led_timer );
            if ( relay_out( ) )
                user_led_set( 1 );
            else
                user_led_set( 0 );
            break;
    }
}

void wifi_init( void )
{
    //wifi配置初始化
//    network_InitTypeDef_st wNetConfig;

//    memset(&wNetConfig, 0, sizeof(network_InitTypeDef_st));
//    wNetConfig.wifi_mode = Station;
//    snprintf(wNetConfig.wifi_ssid, 32, "Honor 9" );
//    strcpy((char*)wNetConfig.wifi_key, "19910911");
//    wNetConfig.dhcpMode = DHCP_Client;
//    wNetConfig.wifi_retry_interval=6000;
//    micoWlanStart(&wNetConfig);

    //wifi状态下led闪烁定时器初始化
    mico_rtos_init_timer( &wifi_led_timer, 1000, (void *) wifi_led_timer_callback, NULL );
    //wifi已连接获取到IP地址 回调
    mico_system_notify_register( mico_notify_DHCP_COMPLETED, (void *) wifi_get_ip_callback, NULL );
    //wifi连接失败回调
    mico_system_notify_register( mico_notify_WIFI_CONNECT_FAILED, (void*) wifi_connect_failed_callback, NULL );
    //wifi连接状态改变回调
    mico_system_notify_register( mico_notify_WIFI_STATUS_CHANGED, (void*) wifi_status_callback, NULL );

    //启动定时器开始进行wifi连接
    if ( !mico_rtos_is_timer_running( &wifi_led_timer ) ) mico_rtos_start_timer( &wifi_led_timer );

}

