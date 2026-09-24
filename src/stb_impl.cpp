#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

// nanosvg (SVG icons from the online libraries)
#include <cstdio>
#include <cstring>
#include <cmath>
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>
