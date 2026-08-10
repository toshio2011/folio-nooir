/* Build the SDK's small, re-entrant TJpgDec implementation for the EPUB
 * fallback decoder. FreeInkBook is not a PlatformIO dependency in this
 * firmware, so keep the implementation in the application source tree. */
#include "../freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd/tjpgd.c"
