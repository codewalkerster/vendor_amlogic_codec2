/*
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef ANDROID_CODEC2_IMG_SUPPORT_H
#define ANDROID_CODEC2_IMG_SUPPORT_H

#include <memory>
#include <C2Component.h>
#include <C2VendorSupport.h>

namespace android {


/* image */
const C2String kJPEGDecoderName = "c2.amlogic.jpeg.decoder";

static C2VendorComponent gC2ImgDecComponents [] = {
    {kJPEGDecoderName, C2VendorCodec::VDEC_JPEG},
};

}  // namespace android

#endif  // ANDROID_CODEC2_IMG_SUPPORT_H

