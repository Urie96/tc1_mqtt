
#define os_log(format, ...)  custom_log("SNTP", format, ##__VA_ARGS__)

#include "main.h"
//#include "user_gpio.h"
#include "user_sntp.h"

/* Callback function when MiCO UTC time in sync to NTP server */

static void sntp_time_call_back( void )
{
    sntp_count++;
    mico_utc_time_t utc_time;
    if (mico_time_get_utc_time(&utc_time) == kNoErr) {
        os_log("UTC time: %ld", utc_time);
    }
    if (first_sntp == 0){
        sntp_stop_auto_time_sync();
        sntp_start_auto_time_sync(3600 * 1000,  sntp_time_call_back);    //1小时校准一次
        first_sntp = 1;
    }
}


void sntp_init(void)
{
    sntp_start_auto_time_sync(60 * 1000,  sntp_time_call_back);        //首次每1分钟时校准一次
}
