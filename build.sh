#export CFLAGS="-w"
#export CXXFLAGS="-w"
# - ARMv8/aarch64 
export PATH=/home/ncl/Downloads/gcc-linaro-6.3.1-2017.02-x86_64_aarch64-linux-gnu/bin:$PATH
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export ODM_WT_EDIT=yes
export VENDOR_EDIT=1
export MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT=yes
#export PATH=/home/ncl/Downloads/aarch64-linux-android-4.9-refs_heads_oreo-mr1-release.tar/bin:$PATH
#export ARCH=arm64
#export CROSS_COMPILE=aarch64-linux-android-
cd /home/ncl/Downloads/A5S-8.1-kernel-source
#make O=out1 V=1
make O=out oppo6765_18511_defconfig
make O=out -j4 > out/build1.log 2>&1

