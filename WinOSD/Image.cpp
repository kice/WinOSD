#ifndef USE_OPENCV
#define STBI_NO_PIC
#define STBI_NO_HDR
#define STBI_NO_PNM

#define STBI_MALLOC(sz)         _aligned_malloc(sz, 512)
#define STBI_REALLOC(p,newsz)   _aligned_realloc(p, newsz, 512)
#define STBI_FREE(p)            _aligned_free(p)

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif
