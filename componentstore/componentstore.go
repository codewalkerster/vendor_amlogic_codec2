//
// Copyright (C) 2023 Amlogic, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

package componentstore

import (
    "android/soong/android"
    "android/soong/cc"
    "strconv"
)

func init() {
    android.RegisterModuleType("componentstore_defaults", componentstoreDefaultsFactory)
}

func componentstoreDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, componentstoreDefaults)
    android.AddLoadHook(module, systemControlIpcWayDefaults)
    return module
}

func componentstoreDefaults(ctx android.LoadHookContext) {
    var cppflags []string
    type props struct {
        Cflags []string
    }
    p := &props{}

    vconfig := ctx.Config().VendorConfig("amlogic_vendorconfig")
    if vconfig.Bool("enable_swcodec") == true {
        cppflags = append(cppflags, "-DSUPPORT_SOFT_VDEC=1 -DSUPPORT_SOFT_AFFMPEG=1")
    }

    if vconfig.Bool("enable_hwcodec") == true {
        cppflags = append(cppflags, "-DSUPPORT_VDEC_AVS=1 -DSUPPORT_VDEC_AVS2=1 -DSUPPORT_VDEC_AVS3=1")
    }

    // config vvc
    if vconfig.Bool("enable_vendor_media_c2_vvc_support") == true {
        cppflags = append(cppflags, "-DVENDOR_MEDIA_VVC_SUPPORT=1")
    }

    p.Cflags = cppflags
    ctx.AppendProperties(p)
}


func systemControlIpcWayDefaults(ctx android.LoadHookContext) {
    var cppflags []string
    type props struct {
        Cflags       []string
        Shared_libs  []string
    }
    p := &props{}

    sdkVersionstr := ctx.Config().PlatformSdkVersion().String()
    SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + sdkVersionstr
    cppflags = append(cppflags, SDKVERSION)
    sdkVersionInt,err := strconv.Atoi(sdkVersionstr)
    if err != nil {
        //fmt.Printf("%v fail to convert", sdkVersionInt)
    } else {
        //fmt.Println("PassthroughDefaults sdkVersion:", sdkVersionInt)
    }
    if sdkVersionInt < 35 {
        p.Shared_libs = append(p.Shared_libs, "vendor.amlogic.hardware.systemcontrol@1.0")
        p.Shared_libs = append(p.Shared_libs, "vendor.amlogic.hardware.systemcontrol@1.1")
    } else {
        p.Shared_libs = append(p.Shared_libs, "vendor.amlogic.hardware.systemcontrol-V1-ndk")
    }

    p.Cflags = cppflags
    ctx.AppendProperties(p)
}

