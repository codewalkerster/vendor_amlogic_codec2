/*
 * Copyright (C) 2023 Amlogic, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "Img"
#include <UltraHdrHelper.h>
#include <cutils/log.h>
#define DUMP_DEBUG_DATA 1
namespace android {
#if 0
    const float BT709RGBtoYUVMatrix[9] = {0.2126,
                                      0.7152,
                                      0.0722,
                                      (-0.2126 / 1.8556),
                                      (-0.7152 / 1.8556),
                                      0.5,
                                      0.5,
                                      (-0.7152 / 1.5748),
                                      (-0.0722 / 1.5748)};

    const float BT2020RGBtoYUVMatrix[9] = {0.2627,
                                       0.6780,
                                       0.0593,
                                       (-0.2627 / 1.8814),
                                       (-0.6780 / 1.8814),
                                       0.5,
                                       0.5,
                                       (-0.6780 / 1.4746),
                                       (-0.0593 / 1.4746)};
#endif
    // remove these once introduced in ultrahdr_api.h
    //const int UHDR_IMG_FMT_48bppYCbCr444 = 101;

#define PROFILE_ENABLE 1

    class Profiler {
        public:
            void timerStart() { gettimeofday(&mStartingTime, nullptr); }

            void timerStop() { gettimeofday(&mEndingTime, nullptr); }

            int64_t elapsedTime() {
                struct timeval elapsedMicroseconds;
                elapsedMicroseconds.tv_sec = mEndingTime.tv_sec - mStartingTime.tv_sec;
                elapsedMicroseconds.tv_usec = mEndingTime.tv_usec - mStartingTime.tv_usec;
                return elapsedMicroseconds.tv_sec * 1000000 + elapsedMicroseconds.tv_usec;
            }

        private:
            struct timeval mStartingTime;
            struct timeval mEndingTime;
    };
#if 0
    static bool writeFile(const char* filename, void*& result, int length) {
        std::ofstream ofd(filename, std::ios::binary);
        if (ofd.is_open()) {
            ofd.write(static_cast<char*>(result), length);
            return true;
        }
        std::cerr << "unable to write to file : " << filename << std::endl;
        return false;
    }

    static bool writeFile(const char* filename, uhdr_raw_image_t* img) {
        std::ofstream ofd(filename, std::ios::binary);
        if (ofd.is_open()) {
            if (img->fmt == UHDR_IMG_FMT_32bppRGBA8888 || img->fmt == UHDR_IMG_FMT_64bppRGBAHalfFloat ||
                img->fmt == UHDR_IMG_FMT_32bppRGBA1010102) {
                char* data = static_cast<char*>(img->planes[UHDR_PLANE_PACKED]);
                int bpp = img->fmt == UHDR_IMG_FMT_64bppRGBAHalfFloat ? 8 : 4;
                const size_t stride = img->stride[UHDR_PLANE_PACKED] * bpp;
                const size_t length = img->w * bpp;
                for (unsigned i = 0; i < img->h; i++, data += stride) {
                    ofd.write(data, length);
                }
                return true;
            } else if ((int)img->fmt == UHDR_IMG_FMT_24bppYCbCr444 ||
                       (int)img->fmt == UHDR_IMG_FMT_48bppYCbCr444) {
                char* data = static_cast<char*>(img->planes[UHDR_PLANE_Y]);
                int bpp = (int)img->fmt == UHDR_IMG_FMT_48bppYCbCr444 ? 2 : 1;
                size_t stride = img->stride[UHDR_PLANE_Y] * bpp;
                size_t length = img->w * bpp;
                for (unsigned i = 0; i < img->h; i++, data += stride) {
                    ofd.write(data, length);
                }
                data = static_cast<char*>(img->planes[UHDR_PLANE_U]);
                stride = img->stride[UHDR_PLANE_U] * bpp;
                for (unsigned i = 0; i < img->h; i++, data += stride) {
                    ofd.write(data, length);
                }
                data = static_cast<char*>(img->planes[UHDR_PLANE_V]);
                stride = img->stride[UHDR_PLANE_V] * bpp;
                for (unsigned i = 0; i < img->h; i++, data += stride) {
                    ofd.write(data, length);
                }
                return true;
            }
            return false;
        }
        std::cerr << "unable to write to file : " << filename << std::endl;
        return false;
    }

#endif
#define READ_BYTES(DESC, ADDR, LEN)                                                             \
    DESC.read(static_cast<char*>(ADDR), (LEN));                                                   \
    if (DESC.gcount() != (LEN)) {                                                                 \
        std::cerr << "failed to read : " << (LEN) << " bytes, read : " << DESC.gcount() << " bytes" \
                  << std::endl;                                                                     \
        return false;                                                                               \
    }



    UltraHdrAppInput::UltraHdrAppInput(uint8_t * inBuffer, size_t inSize,
                   uhdr_color_transfer_t oTf,
                   uhdr_img_fmt_t oFmt, float ratio, bool enableGLES)
      : mOTf(oTf),
        mOfmt(oFmt),
        mEnableGLES(enableGLES),
        mDisplayRatio(ratio){
            mUhdrImage.capacity = inSize;
            mUhdrImage.data_sz = inSize;
            mUhdrImage.data = inBuffer;
            mUhdrImage.cg = UHDR_CG_UNSPECIFIED;
            mUhdrImage.ct = UHDR_CT_UNSPECIFIED;
            mUhdrImage.range = UHDR_CR_UNSPECIFIED;
        };

    int UltraHdrAppInput::decode(uint8_t*& outputFile) {
        ALOGE("decode");
        int mOutSize = 0;
#define RET_IF_ERR(x)                            \
        {                                              \
            uhdr_error_info_t status = (x);              \
            if (status.error_code != UHDR_CODEC_OK) {    \
              if (status.has_detail) {                   \
                std::cerr << status.detail << std::endl; \
              }                                          \
              uhdr_release_decoder(handle);              \
              return 0;                              \
            }                                            \
        }

        uhdr_codec_private_t* handle = uhdr_create_decoder();
        uhdr_error_info_t status = uhdr_dec_set_image(handle, &mUhdrImage);
        ALOGE("status %d",status.error_code);
        RET_IF_ERR(uhdr_dec_set_image(handle, &mUhdrImage))
        status = uhdr_dec_set_out_color_transfer(handle, mOTf);
        ALOGE("--->status %d",status.error_code);
        if (mDisplayRatio > 1.0f)
          status = uhdr_dec_set_out_max_display_boost(handle, mDisplayRatio);
        RET_IF_ERR(uhdr_dec_set_out_color_transfer(handle, mOTf))
        status = uhdr_dec_set_out_img_format(handle, mOfmt);
        ALOGE("--->xxxstatus %d",status.error_code);
        RET_IF_ERR(uhdr_dec_set_out_img_format(handle, mOfmt))
        if (mEnableGLES) {
            RET_IF_ERR(uhdr_enable_gpu_acceleration(handle, mEnableGLES))
        }
        RET_IF_ERR(uhdr_dec_probe(handle))

#ifdef PROFILE_ENABLE
        Profiler profileDecode;
        profileDecode.timerStart();
#endif
        RET_IF_ERR(uhdr_decode(handle))


#undef RET_IF_ERR
        uhdr_raw_image_t* output = uhdr_get_decoded_image(handle);
        uint8_t* outData = (uint8_t*)outputFile;
        int bpp = 4;//(output->fmt == UHDR_IMG_FMT_64bppRGBAHalfFloat) ? 8 : 4;
        uint8_t* inData = static_cast<uint8_t*>(output->planes[UHDR_PLANE_PACKED]);
        size_t widthVal = output->stride[UHDR_PLANE_PACKED];
        size_t widthpack = (widthVal%32 == 0? widthVal:(widthVal+32)/32*32);
        if (output->stride[UHDR_PLANE_PACKED]%32 ==0) {
            widthpack = output->stride[UHDR_PLANE_PACKED];
        }
        const size_t inStride = output->stride[UHDR_PLANE_PACKED] * bpp;
        const size_t outStride = widthpack * bpp;
        for (unsigned i = 0; i < output->h; i++, inData += inStride, outData += outStride) {
            mOutSize += outStride;
            memcpy(outData, inData, inStride);
        }
       //ALOGE("mOutSize %d output->%d inStride %d outStride %d",mOutSize,output->h,inStride,outStride);

        uhdr_release_decoder(handle);
#ifdef PROFILE_ENABLE
        profileDecode.timerStop();
        auto avgDecTime = profileDecode.elapsedTime() / 1000.f;
        printf("Average decode time for res %d x %d is %f ms \n", uhdr_dec_get_image_width(handle),
             uhdr_dec_get_image_height(handle), avgDecTime);
#endif
        return mOutSize;
    }
}