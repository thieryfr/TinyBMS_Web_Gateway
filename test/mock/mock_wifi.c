#include <stdbool.h>

static int s_wifi_sta_restart_count = 0;

void wifi_start_sta_mode(void)
{
    ++s_wifi_sta_restart_count;
}

int test_wifi_get_sta_restart_count(void)
{
    return s_wifi_sta_restart_count;
}

void test_wifi_reset_sta_restart_count(void)
{
    s_wifi_sta_restart_count = 0;
}
