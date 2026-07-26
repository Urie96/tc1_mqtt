
#ifndef __USER_WIFI_H_
#define __USER_WIFI_H_


#include "mico.h"


enum {
   WIFI_STATE_FAIL,
   WIFI_STATE_NOCONNECT,
   WIFI_STATE_CONNECTING,
   WIFI_STATE_CONNECTED,
   WIFI_STATE_NOEASYLINK,
   WIFI_STATE_EASYLINK,
   WIFI_STATE_EASYLINKING,
   WIFI_STATE_SOFTAP,
   WIFI_STATE_SOFTAP_CONFIGURED,
};


extern char wifi_status;
extern void wifi_init(void);
extern void wifi_start_easylink(void);
extern void wifi_start_softap(void);
extern void wifi_stop_softap(void);



#endif
