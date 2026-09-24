#ifndef DEFAULT_MAPPINGS_H
#define DEFAULT_MAPPINGS_H

#include <ShadowMap.h>

// Built-in button mappings (defined in src/default_mappings.cpp). SHADOW uses these for any
// slot that DroidNet hasn't changed, and for everything after `@SM DEFAULTS`.
extern const sm::DefaultMapping defaultButtonMappings[] PROGMEM;
extern const uint8_t defaultButtonMappingsCount;

#endif  // DEFAULT_MAPPINGS_H
