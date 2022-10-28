#!/bin/bash
SCRIPT_DIR=$(cd `dirname $0` && pwd)
#################################
# Configuable parameter         #
#################################
DEVICE=spider
USERNAME=rcar
HOSTNAME=${DEVICE}

KERNEL_CONFIG_APPEND="
# for USB3 support
CONFIG_FW_LOADER=y
CONFIG_FIRMWARE_IN_KERNEL=y
CONFIG_EXTRA_FIRMWARE=\"r8a779x_usb3_v2.dlmem r8a779x_usb3_v3.dlmem\"
CONFIG_EXTRA_FIRMWARE_DIR=\"firmware\"
# Avoid waiting timeout for loading USB firmware
CONFIG_FW_LOADER_USER_HELPER=n
CONFIG_FW_LOADER_USER_HELPER_FALLBACK=n
# For fleetwise
CONFIG_CAN_ISOTP=m
CONFIG_CAN_VCAN=m
# For S4
CONFIG_GPIO_SYSFS=y
CONFIG_DMATEST=m
CONFIG_PCI_ENDPOINT_TEST=y
CONFIG_SPI_SH_MSIOF=y
CONFIG_SPI_SPIDEV=y
"
#################################

if [[ $# < 2 ]]; then
    echo "Usage: $0 <Ubuntu version> <device_name> (<gui_option>)"
    echo "    Ubuntu version(required):"
    echo "        18.04.3, 20.04.4, etc..."
    echo "        Please visit http://cdimage.ubuntu.com/ubuntu-base/releases/ ."
    echo "    Target device(required):"
    echo "        spider, ccpf-sk, ulcb"
    echo "    gui_option(optional):"
    echo "        -g: install gnome desktop. But it may be slow because GPU is not active."
    echo "    SD card option(optional) For S4 Spider:"
    echo "        -sd: use sdcard instead of eMMC."
    echo "Required package:"
    echo "    sudo apt install debootstrap qemu-user-static binfmt-support gcc-aarch64-linux-gnu"
    echo "    # Also required the software which is needed to build linux kernel."
    echo "memo:"
    echo "    debootstrap is not needed, but dependencies are required."
    exit
fi

NET_DEV=eth0
if [[ "$2" == "spider" ]]; then
    DEVICE=spider
    NET_DEV=tsn0
elif [[ "$2" == "ccpf-sk" ]]; then
    DEVICE=ccpf-sk
elif [[ "$2" == "ulcb" ]]; then
    DEVICE=ulcb
else
    echo error: Check target board
    exit
fi

if [[ "$(id -u)" != "0" ]]; then
    echo Please run as root!; exit 1
fi

if [[ "`update-binfmts --display | grep aarch64`" == "" ]]; then
    echo "qemu may not be installed."
    echo "Please install it by following command:"
    echo "    sudo apt install qemu-user-static"
    exit
fi
if [[ "`update-binfmts --display | grep aarch64 | grep enable`" == "" ]]; then
    echo "qemu-aarch64 is not enabled."
    echo "Please enable it by following command:"
    echo "    sudo update-binfmts --enable qemu-aarch64"
    exit
fi
if [[ ! -e "`which flex`" ]]; then
    echo "It seems that depencencies are not installed"
    echo "sudo apt-get install libncurses-dev gawk flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf"
fi

ADDITIONAL_PACKAGE=""
ROOTFS_APPEND=""
# Gnome option
if [[ "$(echo $@' '| grep ' -g ')" != "" ]]; then
    ADDITIONAL_PACKAGE=" gnome libegl-mesa0"
    ROOTFS_APPEND="-gnome"
fi

SDCARD_OPTION=""
if [[ "$(echo $@' '| grep ' -sd ')" != "" ]]; then
    if [[ "${DEVICE}" == "spider" ]]; then
        SDCARD_OPTION="True"
    fi
fi

DHCP_CONF="
[Match]
Name=${NET_DEV}
[Network]
DHCP=ipv4
"

NAMESERVER=$(cat /etc/resolv.conf | grep nameserver | head -n 1 | awk '{print $2}')
UBUNTU_ROOTFS_NAME=ubuntu-base-$1-base-arm64.tar.gz
UBUNTU_VER=$1
UBUNTU_ROOTFS_URL=http://cdimage.ubuntu.com/ubuntu-base/releases/${UBUNTU_VER}/release/${UBUNTU_ROOTFS_NAME}

ROOTFS=${ROOTFS:-Ubuntu-${UBUNTU_VER}${ROOTFS_APPEND}-rootfs}
SDCARD_IMAGE_NAME=${ROOTFS}-image-rcar-${DEVICE}.ext4
if [[ "${SDCARD_OPTION}" == "True" ]]; then
    SDCARD_IMAGE_NAME=${ROOTFS}-image-rcar-${DEVICE}-sdcard.ext4
fi

# Cleanup
rm -rf ${SDCARD_IMAGE_NAME} ${SDCARD_IMAGE_NAME}.gz
rm -rf ${ROOTFS}

# Downlaod ubuntu base
mkdir -p ${ROOTFS}
wget ${UBUNTU_ROOTFS_URL} -O- | tar zx -C ${ROOTFS}
QEMU_BIN_PATH=$(which qemu-aarch64-static)
cp ${QEMU_BIN_PATH} ./${ROOTFS}/${QEMU_BIN_PATH}

# Prepare rootfs
chroot "${ROOTFS}" sh -c " \
    export DEBIAN_FRONTEND=noninteractive \
    && echo nameserver ${NAMESERVER} >/etc/resolv.conf \
    && apt update \
    && apt install -y apt-utils perl-modules \
    && apt install -y ubuntu-standard \
    && apt install -y vim net-tools ssh sudo tzdata rsyslog udev iputils-ping \
    && apt install -y unzip curl kmod iproute2 git python3-pip nano \
    && apt upgrade -y \
    && echo \"${DHCP_CONF}\" > /etc/systemd/network/01-${NET_DEV}.network \
    && useradd -m -s /bin/bash -G sudo ${USERNAME} \
    && echo ${USERNAME}:${USERNAME} | chpasswd \
    && echo \"${USERNAME}   ALL=(ALL) NOPASSWD:ALL\" >> /etc/sudoers \
    && echo ${HOSTNAME} > /etc/hostname \
    && systemctl enable systemd-networkd \
    && cp /usr/share/systemd/tmp.mount /etc/systemd/system/tmp.mount \
    && systemctl enable tmp.mount \
    && systemctl enable systemd-resolved \
    && rm /etc/resolv.conf \
    && ln -s /run/systemd/resolve/resolv.conf /etc/resolv.conf \
    && echo 127.0.0.1 localhost > /etc/hosts \
    && echo 127.0.1.1 ${HOSTNAME} >> /etc/hosts \
    && depmod -a \
    && apt clean \
    && exit \
"

# Prepare linux-kernel for R-Car
cd ${SCRIPT_DIR}
if [ ! -e linux-bsp-${DEVICE} ]; then
    if [[ "${DEVICE}" == "spider" ]] ;then
        git clone --depth 1 https://github.com/renesas-rcar/linux-bsp/ -b v5.10.41/rcar-5.1.6.rc3 linux-bsp-${DEVICE}
    else
        git clone --depth 1 https://github.com/renesas-rcar/linux-bsp/ -b v5.10.41/rcar-5.1.4 linux-bsp-${DEVICE}
    fi
    cd linux-bsp-${DEVICE}
    if [[ "${DEVICE}" == "ccpf-sk" ]]; then
        wget https://github.com/renesas-rcar/meta-renesas-ccpf/archive/refs/heads/dunfell.zip
        unzip dunfell.zip
        git am ./meta-renesas-ccpf-dunfell/meta-rcar-gen3/recipes-kernel/linux/linux-renesas/*.patch
        rm -rf dunfell.zip ./meta-renesas-ccpf-dunfell
    fi
else
    cd linux-bsp-${DEVICE}
fi

git reset --hard ; git clean -df
if [[ "${SDCARD_OPTION}" == "True" ]]; then
    sed -i 's/#ifdef S4_USE_SD/#define S4_USE_SD\n&/' ./arch/arm64/boot/dts/renesas/r8a779f0-spider-cpu.dtsi
fi
echo "${KERNEL_CONFIG_APPEND}" >> ./arch/arm64/configs/defconfig
mkdir -p firmware && cd firmware
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/r8a779x_usb3_v2.dlmem
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/r8a779x_usb3_v3.dlmem
cd ../
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig Image dtbs -j$(grep -c processor /proc/cpuinfo)
cp -L arch/arm64/boot/Image -t ../${ROOTFS}/boot
find ./arch/arm64/boot/ -name "*${DEVICE}.dtb" | xargs sudo cp \
    -t ../${ROOTFS}/boot
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules -j$(grep -c processor /proc/cpuinfo)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    INSTALL_MOD_PATH=../${ROOTFS}/ modules_install
cd ../

# Prepare sdcard image (+1GB free space)
BSIZE_MEGA=$(( 1000 + $(sudo du -hsm ./${ROOTFS} | head -1 | cut -f1) ))
dd of=image.img count=0 seek=1 bs=${BSIZE_MEGA}M
LOOP_DEV=$(sudo losetup -f)
losetup ${LOOP_DEV} image.img
# MBR=msdos, GPT=gpt
parted ${LOOP_DEV} -s mklabel msdos -s mkpart primary ext4 4096s 100%
mkfs.ext4 ${LOOP_DEV}p1
mount ${LOOP_DEV}p1 /mnt
cp -rp ./${ROOTFS}/* -t /mnt
sync
umount /mnt
losetup -d ${LOOP_DEV}

# Prepare to release
mv -f image.img ${SDCARD_IMAGE_NAME}
gzip -k ${SDCARD_IMAGE_NAME}
