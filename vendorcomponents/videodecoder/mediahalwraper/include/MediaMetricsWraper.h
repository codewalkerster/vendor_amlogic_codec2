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

#ifndef MEDIA_METRICS_WRAPER_H_
#define MEDIA_METRICS_WRAPER_H_

#include <MediaMetricsData.h>
#include <stdint.h>
#include <inttypes.h>
#include <list>

namespace android {

class MediaMetricsWraper {
public:
    MediaMetricsWraper(int32_t id);
    ~MediaMetricsWraper();
    bool queueFrameInputInfo(metrics_frame_info* frame);
    bool getMetricsInfo(media_metrics_info* info);

private:
    void* mSession;
};

}
#endif
