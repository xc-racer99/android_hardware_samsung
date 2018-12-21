########################################################################### ###
#@Title         Root makefile for s5pc110 Android.  Builds everything else.
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

# If a TARGET_PRODUCT is specified but not a TARGET_DEVICE, try to
# derive the TARGET_DEVICE from TARGET_PRODUCT.
#
ifeq ($(TARGET_DEVICE),)
override TARGET_DEVICE := \
 $(patsubst mini_%,%,$(patsubst full_%,%,$(TARGET_PRODUCT)))
endif

ifeq ($(TARGET_DEVICE),)
override TARGET_DEVICE := fascinate4gubi
endif

# Customize this build as per the TARGET_DEVICE setting
#
SGXCORE						:= 540
SGX_CORE_REV				:= 120
# TODO - Remove?
HAL_VARIANT					:= omap4

# TODO check if can be removed?
# SGX_DYNAMIC_TIMING_INFO := 1

SUPPORT_LINUX_USING_WORKQUEUES := 1
SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED := 1

SUPPORT_PVRSRV_ANDROID_SYSTRACE := 1

SUPPORT_ION := 1
SUPPORT_DMABUF := 0

# Enable PVR_DPF prints for fatal/error
PVRSRV_NEED_PVR_DPF := 1
PVRSRV_NEW_PVR_DPF := 1

# TODO: Set SUPPORT_PVRSVR_DEVICE_CLASS to 0
#DISPLAY_CONTROLLER should not be used for DRM based display controller
ifneq ($(SUPPORT_PVRSRV_DEVICE_CLASS),0)
DISPLAY_CONTROLLER := s3c_lcd
endif

PVR_SYSTEM := s5pc110

# We have sufficent memory on S5PC110 platforms, so we can spare to make the
# pool larger, and have higher resolutions which benefit from it.
#
PVR_LINUX_MEM_AREA_POOL_MAX_PAGES ?= 10800

include ../common/android/paths.mk
include ../common/android/armv7-a.mk
include ../common/android/features.mk

LDM_PLATFORM := 1
PVR_LDM_PLATFORM_PRE_REGISTERED := 1
PVR_LDM_PLATFORM_PRE_REGISTERED_DEV := "\"pvrsrvkm\""

ifneq ($(strip $(KERNELDIR)),)
 ifeq ($(call kernel-version-at-least,2,6,39),true)
  ifeq ($(LDM_PLATFORM),1)
   PVR_LDM_PLATFORM_PRE_REGISTERED := 1
   PVR_LDM_PLATFORM_PRE_REGISTERED_DEV := "\"pvrsrvkm\""
  endif
 endif
 ifeq ($(call kernel-version-at-least,3,8,13),true)
  PVR_LDM_DEVICE_TREE := 1
 endif
else
 $(warning "KERNELDIR is not set, so can't feature check")
 $(warning "Assuming we want LDM if enabled")
 ifeq ($(LDM_PLATFORM),1)
  PVR_LDM_PLATFORM_PRE_REGISTERED := 1
  PVR_LDM_PLATFORM_PRE_REGISTERED_DEV := "\"pvrsrvkm\""
 endif
endif

ifneq ($(LDM_PLATFORM),1)
SUPPORT_LINUX_USING_WORKQUEUES := 0
SUPPORT_LINUX_USING_SHARED_WORKQUEUES := 1
SUPPORT_ACTIVE_POWER_MANAGEMENT := 0
ifneq ($(SUPPORT_PVRSRV_DEVICE_CLASS),0)
DISPLAY_CONTROLLER := s3c_lcd
DISPLAY_CONTROLLER_COMPONENT := linux_framebuffer
endif
else # LDM_PLATFORM != 1
# TODO: we should set DEVICE_CLASS to zero
ifneq ($(SUPPORT_PVRSRV_DEVICE_CLASS),0)
DISPLAY_CONTROLLER_COMPONENT := s3c_lcd
endif
endif # LDM_PLATFORM != 1

ifeq ($(SUPPORT_DRI_DRM),1)
ifeq ($(PVR_LDM_PLATFORM_PRE_REGISTERED),1)
PVR_DRI_DRM_PLATFORM_DEV := 1
PVR_DRI_DRM_STATIC_BUS_ID := 1
PVR_DRI_DRM_DEV_BUS_ID := "\"platform:pvrsrvkm"\"
else
PVR_DRI_DRM_NOT_PCI := 1
KERNEL_COMPONENTS += linux_drm
endif
ifneq ($(SUPPORT_PVRSRV_DEVICE_CLASS),0)
EXTRA_PVRSRVKM_COMPONENTS += $(DISPLAY_CONTROLLER_COMPONENT)
endif
EXTRA_KBUILD_SOURCE := $(KERNELDIR)
# FIXME: Only required for comparison with X's KM
PVR_SECURE_DRM_AUTH_EXPORT := 1
else # SUPPORT_DRI_DRM == 1
ifneq ($(SUPPORT_PVRSRV_DEVICE_CLASS),0)
KERNEL_COMPONENTS += $(DISPLAY_CONTROLLER_COMPONENT)
endif
endif # SUPPORT_DRI_DRM == 1

PVR_ANDROID_USE_WINDOW_TRANSFORM_HINT := 1

PVR_ANDROID_PLATFORM_HAS_LINUX_FBDEV := 1

# Handle Google's OMAP-based products
#
ifneq ($(filter telusgalaxys4gmtd galaxys4gmtd galaxysmtd galaxysbmtd fascinate4gubi galaxys4gubi galaxysubi tuna maguro toro mysid yakju,$(TARGET_DEVICE)),)
# These default on in telusgalaxys4gmtd galaxys4gmtd galaxysmtd galaxysbmtd fascinate4gubi galaxys4gubi galaxysubi tuna_defconfig
PVRSRV_USSE_EDM_STATUS_DEBUG ?= 1
PVRSRV_DUMP_MK_TRACE ?= 1
endif

ifeq ($(NO_HARDWARE),1)
ifeq ($(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED),1)
$(info WARNING: SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED=1 is incompatible with NO_HARDWARE=1)
$(info WARNING: Setting SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED=0 and switching to dc_nohw)
override SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED := 0
ifneq ($(SUPPORT_PVRSRV_DEVICE_CLASS),0)
override DISPLAY_CONTROLLER := dcnohw
KERNEL_COMPONENTS += dc_nohw
endif
endif # SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED == 1
endif # NO_HARDWARE == 1

# we are not using PVR composerhal
#PVR_ANDROID_COMPOSERHAL ?= s5pc110

include ../config/core.mk
include ../common/android/extra_config.mk
include ../common/dridrm.mk
include ../common/opencl.mk

