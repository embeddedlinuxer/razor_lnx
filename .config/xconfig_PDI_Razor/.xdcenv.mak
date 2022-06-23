#
_XDCBUILDCOUNT = 0
ifneq (,$(findstring path,$(_USEXDCENV_)))
override XDCPATH = D:/sdk/win/ti/uia_2_30_01_02/packages;D:/sdk/win/ti/bios_6_76_03_01/packages;D:/sdk/win/ti/pdk_omapl138_1_0_11/packages;D:/workspace_home/PDI_Razor/.config
override XDCROOT = D:/sdk/win/ti/xdctools_3_55_02_22_core
override XDCBUILDCFG = ./config.bld
endif
ifneq (,$(findstring args,$(_USEXDCENV_)))
override XDCARGS = 
override XDCTARGETS = 
endif
#
ifeq (0,1)
PKGPATH = D:/sdk/win/ti/uia_2_30_01_02/packages;D:/sdk/win/ti/bios_6_76_03_01/packages;D:/sdk/win/ti/pdk_omapl138_1_0_11/packages;D:/workspace_home/PDI_Razor/.config;D:/sdk/win/ti/xdctools_3_55_02_22_core/packages;..
HOSTOS = Windows
endif
