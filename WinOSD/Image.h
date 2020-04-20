#pragma once

// TODO: This should be in compiler command line
// #define USE_OPENCV

#ifdef USE_OPENCV
#include <opencv2/opencv.hpp>
#else
#define STBI_NO_PIC
#define STBI_NO_HDR
#define STBI_NO_PNM

#define STBI_MALLOC(sz)         _aligned_malloc(sz, 512)
#define STBI_REALLOC(p,newsz)   _aligned_realloc(p, newsz, 512)
#define STBI_FREE(p)            _aligned_free(p)

#include <stb_image.h>
#include <stb_image_resize.h>
#include <stb_image_write.h>
#endif

#include <cstdint>
#include <string>

#include "logging.h"

namespace {
inline void bitblt(void *dstp, size_t dst_stride, const void *srcp, size_t src_stride, size_t row_size, size_t height)
{
    if (height) {
        if (src_stride == dst_stride && src_stride == row_size) {
            memcpy(dstp, srcp, row_size * height);
        } else {
            const uint8_t *srcp8 = (const uint8_t *)srcp;
            uint8_t *dstp8 = (uint8_t *)dstp;
            for (size_t i = 0; i < height; i++) {
                memcpy(dstp8, srcp8, row_size);
                srcp8 += src_stride;
                dstp8 += dst_stride;
            }
        }
    }
}
}

class Image
{
public:
    enum PixelOrder
    {
        BGR,
        RGB,
    };

    static Image open(const uint8_t *image_data, size_t len)
    {
        if (len == 0) {
            return Image();
        }

#ifdef USE_OPENCV
        // it should be BGR or BGRA
        auto im = cv::imdecode(cv::Mat(1, (int)len, CV_8UC1, (void *)image_data), cv::IMREAD_UNCHANGED);
        if (im.empty()) {
            DBG << "Unable to decode image";
            return Image();
        }

        int och = im.channels();
        if (och != 4) {
            cv::Mat rgba(im.size(), CV_MAKE_TYPE(im.depth(), 4));

            if (och == 3) {
                cv::cvtColor(im, rgba, cv::COLOR_BGR2BGRA);
            } else if (och == 1) {
                cv::cvtColor(im, rgba, cv::COLOR_GRAY2BGRA);
            } else {
                DBG << "Unknown image type";
                return Image();
            }

            im = std::move(rgba);
        }

        return Image(std::move(im));
#else
        int w, h, ch;
        uint8_t *data = stbi_load_from_memory(image_data, (int)len, &w, &h, &ch, 4);
        if (data == nullptr) {
            return Image();
        }

        // If it dose not have alpha channel, then it should be RGB; otherwise assume it is BGR

        return Image(data, w, h, 4, ch == 3 ? RGB : BGR);
#endif
    }

    Image() :
        data(nullptr), width(0), height(0), ch(0), order(BGR)
    {}

#ifdef USE_OPENCV
    Image(cv::Mat &&im, PixelOrder _order = BGR) // OpenCV default to BGR
    {
        if (im.empty()) {
            return;
        }

        image = im;
        data = image.data;
        width = image.cols;
        height = image.rows;
        ch = image.channels();
        order = _order;
    }
#else
    Image(uint8_t *_data, int _w, int _h, int _ch, PixelOrder _order) :
        data(_data), width(_w), height(_h), ch(_ch), order(_order)
    {}
#endif

    // don't copy image
    Image(const Image &other) = delete;
    Image &operator=(const Image &other) = delete;

    Image(Image &&other) noexcept
    {
#ifdef USE_OPENCV
        if (other.image.empty()) {
            image = cv::Mat();
        } else {
            image = std::move(other.image);
        }

        order = other.order;

        data = image.data;
        width = image.cols;
        height = image.rows;
        ch = image.channels();
#else
        if (other.data == nullptr) {
            return;
        }

        order = other.order;

        data = other.data;
        width = other.width;
        height = other.height;
        ch = other.ch;

        other.data = nullptr;
        return;
#endif
    }

    Image &operator=(Image &&other) noexcept
    {
#ifdef USE_OPENCV
        if (other.image.empty()) {
            image = cv::Mat();
        } else {
            image = std::move(other.image);
        }

        order = other.order;

        data = image.data;
        width = image.cols;
        height = image.rows;
        ch = image.channels();
        return *this;
#else
        if (other.data == nullptr) {
            return *this;
        }

        order = other.order;

        data = other.data;
        width = other.width;
        height = other.height;
        ch = other.ch;

        other.data = nullptr;
        return *this;
#endif
    }

    ~Image()
    {}

    bool opened() const
    {
#ifdef USE_OPENCV
        return !image.empty();
#else
        return data != nullptr;
#endif
    }

    operator bool() const
    {
        return opened();
    }

    void save(const std::string &filename) const
    {
#ifdef USE_OPENCV
        cv::imwrite(filename, image);
#else
        auto pos = filename.rfind('.');
        if (pos == std::string::npos) {
            // no file extension
            return;
        }

        auto ext = filename.substr(pos);

        if (ext == ".jpg") {
            stbi_write_jpg(filename.c_str(), width, height, ch, data, 90);
        } else if (ext == ".png") {
            stbi_write_png(filename.c_str(), width, height, ch, data, width * ch);
        } else if (ext == ".bmp") {
            stbi_write_bmp(filename.c_str(), width, height, ch, data);
        } else if (ext == ".tga") {
            stbi_write_tga(filename.c_str(), width, height, ch, data);
        } else {
            DBG << "Unknown file format: " << ext;
        }
#endif
    }

    Image resize(int w, int h) const
    {
#ifdef USE_OPENCV
        if (image.empty()) {
            return Image();
        }

        cv::Mat resized;
        cv::resize(image, resized, { w, h }, 0, 0, cv::INTER_CUBIC);

        return Image(std::move(resized));
#else
        if (data == nullptr || w <= 0 || h <= 0) {
            return Image();
        }

        uint8_t *resized = (uint8_t *)STBI_MALLOC(w * h * ch);
        if (resized == nullptr) {
            return Image();
        }

        auto ret = stbir_resize_uint8(data, width, height, width * ch,
                                      resized, w, h, w * ch, ch);
        if (ret == 0) {
            STBI_FREE(resized);
            return Image();
        }

        return Image(resized, w, h, ch, order);
#endif
    }

    Image crop(int left, int top, int right, int bottom) const
    {
#ifdef USE_OPENCV
        if (image.empty()) {
            return Image();
        }

        int w = width - left - right;
        int h = height - top - bottom;

        if (w <= 0 || h <= 0) {
            return Image();
        }

        cv::Mat resized = image(cv::Rect(left, top, w, h)).clone();
        return Image(std::move(resized));
#else
        if (data == nullptr) {
            return Image();
        }

        int w = width - left - right;
        int h = height - top - bottom;

        if (w <= 0 || h <= 0) {
            return Image();
        }

        uint8_t *resized = (uint8_t *)STBI_MALLOC(w * h * ch);
        if (resized == nullptr) {
            return Image();
        }

        bitblt(resized, w * ch,
               data + (width * top + left) * ch, height * ch,
               w * ch, h);

        return Image(resized, w, h, ch, order);
#endif
    }

    PixelOrder order;
    uint8_t *data;
    int width, height, ch;

private:
#ifdef USE_OPENCV
    cv::Mat image;
#endif
};
