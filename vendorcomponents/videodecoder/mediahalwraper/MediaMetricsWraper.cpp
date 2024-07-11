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
#define LOG_TAG "MediaMetricsWraper"

#include <stdlib.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h>
#include <utils/Log.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "MediaMetricsWraper.h"
#include <C2VendorProperty.h>
#include <C2VendorDebug.h>


namespace android {

struct MediaMetricsWraper_ops {
   MediaMetricsWraper_ops();
   ~MediaMetricsWraper_ops();
   bool (*connect)(int32_t, void**) {NULL};
   bool (*disconnect)(void*) { NULL };
   bool (*queueFrameInputInfo)(void*, metrics_frame_info*) {NULL};
   bool (*getMetricsInfo)(void*, media_metrics_info*) {NULL};
private:
   void* libHandle {NULL};
};

MediaMetricsWraper_ops::MediaMetricsWraper_ops() {
    if (!libHandle) {
        libHandle = dlopen("libmediahal_mediametrics.so", RTLD_NOW);
    }
    if (libHandle) {
        typedef bool (*connect_t)(int32_t, void**);
        connect = (connect_t) dlsym(libHandle, "MediaMetrics_connect");
        if (!connect) {
            ALOGE("not find MediaMetrics_connect symbol");
        }

        typedef bool (*disconnect_t)(void*);
        disconnect = (disconnect_t) dlsym(libHandle, "MediaMetrics_disconnect");
        if (!disconnect) {
            ALOGE("not find MediaMetrics_disconnect symbol");
        }

        typedef bool (*queueFrameInputInfo_t)(void*, metrics_frame_info*);
        queueFrameInputInfo = (queueFrameInputInfo_t) dlsym(libHandle, "MediaMetrics_queueFrameInputInfo");
        if (!queueFrameInputInfo) {
            ALOGE("not find MediaMetrics_queueFrameInputInfo symbol");
        }

        typedef bool (*getMetricsInfo_t)(void*, media_metrics_info*);
        getMetricsInfo = (getMetricsInfo_t) dlsym(libHandle, "MediaMetrics_getMetricsInfo");
        if (!getMetricsInfo) {
            ALOGE("not find MediaMetrics_getMetricsInfo symbol");
        }
    } else {
        ALOGE("dlopen libmediahal_mediametrics.so error:%s", dlerror());
    }
}

MediaMetricsWraper_ops::~MediaMetricsWraper_ops() {
    if (libHandle) {
        dlclose(libHandle);
        libHandle = NULL;
    }
}

static MediaMetricsWraper_ops gMediaMetrics;

MediaMetricsWraper::MediaMetricsWraper(int32_t id) :
    mSession(NULL) {
    if (!gMediaMetrics.connect ||
        !gMediaMetrics.disconnect ||
        !gMediaMetrics.queueFrameInputInfo ||
        !gMediaMetrics.getMetricsInfo) {
        ALOGE("libmediahal_mediametrics.so symbol not load normal, need check");
        return;
    }
    gMediaMetrics.connect(id, &mSession);
}

MediaMetricsWraper::~MediaMetricsWraper() {
    gMediaMetrics.disconnect(mSession);
}

bool MediaMetricsWraper::queueFrameInputInfo(metrics_frame_info* frame) {
    return gMediaMetrics.queueFrameInputInfo(mSession, frame);
}

bool MediaMetricsWraper::getMetricsInfo(media_metrics_info* info) {
    return gMediaMetrics.getMetricsInfo(mSession, info);
}

}
