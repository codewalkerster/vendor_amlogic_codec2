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

#ifndef C2_SOFT_IMAGE_HELP_H_
#define C2_SOFT_IMAGE_HELP_H_

#include <sys/time.h>
#include <string.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include "ultrahdr_api.h"
#include <Codec2BufferUtils.h>
namespace android {
class UltraHdrAppInput {
public:
    UltraHdrAppInput(uint8_t * inBuffer,size_t inSize,
                   uhdr_color_transfer_t oTf,
                   uhdr_img_fmt_t oFmt, float ratio, bool enableGLES);
virtual ~UltraHdrAppInput(){};
    int decode(uint8_t*& outputFile);
private:
    const uhdr_color_transfer_t mOTf;
    const uhdr_img_fmt_t mOfmt;
    const bool mEnableGLES;
    float mDisplayRatio;
    uhdr_compressed_image_t mUhdrImage{};

};
}  // namespace android

#endif
