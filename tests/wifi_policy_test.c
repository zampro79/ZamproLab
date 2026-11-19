#include <assert.h>
#include "zam_wifi_policy.h"
/* Persisted v1 mode values must remain compatible with existing records. */
_Static_assert(ZAM_WIFI_AP == 1, "legacy AP mode changed");
_Static_assert(ZAM_WIFI_STA == 2, "legacy STA mode changed");
_Static_assert(ZAM_WIFI_APSTA == 3, "legacy APSTA mode changed");
_Static_assert(ZAM_WIFI_AUTO == 4, "auto mode changed");
int main(void) {
 assert(zam_wifi_resolve_mode(ZAM_WIFI_AUTO,false)==ZAM_WIFI_AP);
 assert(zam_wifi_resolve_mode(ZAM_WIFI_AUTO,true)==ZAM_WIFI_APSTA);
 for (int m=ZAM_WIFI_AP;m<=ZAM_WIFI_APSTA;m++) {
  assert(zam_wifi_resolve_mode(m,false)==(zam_wifi_mode_t)m);
  assert(zam_wifi_resolve_mode(m,true)==(zam_wifi_mode_t)m);
 }
 for (int m=ZAM_WIFI_AP;m<=ZAM_WIFI_AUTO;m++) {
  for (int enabled=0;enabled<=1;enabled++) {
   zam_wifi_mode_t target=0;
   assert(zam_wifi_sta_transition(m,enabled,true,&target));
   assert(target==(enabled?ZAM_WIFI_APSTA:ZAM_WIFI_AP));
   assert(zam_wifi_sta_transition(m,enabled,false,&target)==(m!=ZAM_WIFI_STA));
  }
 }
 zam_wifi_mode_t target=0;
 assert(!zam_wifi_sta_transition(0,true,true,&target));
 assert(!zam_wifi_sta_transition(ZAM_WIFI_AP,true,true,NULL));
 return 0;
}
