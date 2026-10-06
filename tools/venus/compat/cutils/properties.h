/* AOSP's <cutils/properties.h>, as much of it as virglrenderer's Mesa util code uses, on the NDK's
 * system property API. */
#pragma once
#include <string.h>
#include <sys/system_properties.h>

#define PROPERTY_KEY_MAX 32
#define PROPERTY_VALUE_MAX PROP_VALUE_MAX

static inline int
property_get(const char *key, char *value, const char *default_value)
{
   int len = __system_property_get(key, value);
   if (len > 0)
      return len;
   if (!default_value) {
      value[0] = '\0';
      return 0;
   }
   strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
   value[PROPERTY_VALUE_MAX - 1] = '\0';
   return (int)strlen(value);
}
