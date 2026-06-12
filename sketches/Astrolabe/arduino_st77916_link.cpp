#if defined(ASTROLABE_PLATFORM_185B)

// The Arduino GFX 1.5.0 package does not always compile ST77916.cpp into the
// library archive for this environment, so we include it explicitly to provide
// the symbols required by the Astrolabe 1.85B build.
#include <display/Arduino_ST77916.cpp>

#endif

