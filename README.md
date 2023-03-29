# CSI: a Complete Guide
This is a complete guide for how to use RT-AC86U for generate and collect channel state information Wi-Fi data in real time.
# Table of Contents
- [Requirements](#requirements)
- [Setup](#setup)
    - [Setting up the Receiver](#setting-up-the-receiver)
        - [Asus Merlin-WRT installation and setup](#asus-merlin-wrt-installation-and-setup)
        - [Easy Way](#easy-way)
        - [Manual Way](#manual-way)
        - [Persistence](#persistence)

    - [Setting up the Sender](#setting-up-the-sender)
        - [Asus Merlin-WRT installation and setup](#asus-merlin-wrt-installation-and-setup-1)
        - [Easy Way](#easy-way-1)
        - [Manual Way](#manual-way-1)  
        - [Persistence](#persistence)

    - [Setting up Docker](#setting-up-docker)

- [Usage](#usage)
- [Additional Tools](#additional-tools)
    - [CSV Generator](#csv-generator)

# Requirements
- Linux / MacOS machine
- Ubuntu Virtual Machine (Strongly Recommended, I used  Xubuntu 16.04 LTS)
- Two RT-AC86U routers
- Docker

# Setup
## Setting up the Receiver
### Asus Merlin-WRT installation and setup
- Download the latest release from https://sourceforge.net/projects/asuswrt-merlin/files/RT-AC86U/Release/
- I used [this](https://sourceforge.net/projects/asuswrt-merlin/files/RT-AC86U/Release/RT-AC86U_386.10_0.zip/download)
- Unzip
- Go to AC86U's web interface
- In Administration > Firmware Upgrade > Manual firmware upload
- Upload the RT-AC86U_386.10_0_cferom_ubi.w file
- Enable SSH
### Easy Way
This is an easy method to have all set up using precompiled files. If you have any problem is recommended to proceed  the manual way.
- Download and extract precompiled files from the Rx folder.
- Copy the contents to the `/jffs/` partition of your router: `scp ./Rx/* admin@<routers-ip>:/jffs/`
- Make `nexutil`, `makecsiparams`, and `tcpdump` executable: `ssh admin@<routers-ip> "/bin/chmod +x /jffs/nexutil /jffs/mcp /jffs/tcpdump "`

### Manual way


### 1. Nexmon Installation
1. Install dependencies `udo apt-get install git gawk qpdf flex bison`
2. Only necessary for x86_64 systems, install i386 libs:
    ```
    sudo dpkg --add-architecture i386
    sudo apt update
    sudo apt install libc6:i386 libncurses5:i386 libstdc++6:i386
    ```
3. Clone the nexmon base repository: `git clone https://github.com/seemoo-lab/nexmon.git`.
4. Navigate to the previously cloned nexmon directory and execute `source setup_env.sh` to set 
   a couple of environment variables.
5. Run `make` to extract ucode, templateram and flashpatches from the original firmwares.
6. Navigate to patches/bcm4366c0/10_10_122_20/ and clone this repository: 
    `git clone https://github.com/seemoo-lab/nexmon_csi.git`
7. Enter the created subdirectory nexmon_csi and run 
    `make install-firmware REMOTEADDR=<address of your rt-ac86u>` to compile our firmware patch and install it on your RT-AC86U router.
8. Clone the aarch64 toolchain repository: `git clone https://github.com/RMerl/am-toolchains.git`.
9. Set the compile environment:
    ```
    export AMCC=$(pwd)/am-toolchains/brcm-arm-hnd/crosstools-aarch64-gcc-5.3-linux-4.1-glibc-2.22-binutils-2.25/usr/bin/aarch64-buildroot-linux-gnu-
    export LD_LIBRARY_PATH=$(pwd)/am-toolchains/brcm-arm-hnd/crosstools-aarch64-gcc-5.3-linux-4.1-glibc-2.22-binutils-2.25/usr/lib
    ```
10. Go back to the nexmon repository root, compile and install nexutil:
    ```
    cd utilities/libnexio
    ${AMCC}gcc -c libnexio.c -o libnexio.o -DBUILD_ON_RPI
    ${AMCC}ar rcs libnexio.a libnexio.o
    cd ../nexutil
    echo "typedef uint32_t uint;" > types.h
    sed -i 's/argp-extern/argp/' nexutil.c
    ${AMCC}gcc -static -o nexutil nexutil.c bcmwifi_channels.c b64-encode.c b64-decode.c -DBUILD_ON_RPI -DVERSION=0 -I. -I../libnexio -I../../patches/include -L../libnexio/ -lnexio
    scp nexutil admin@<address of your rt-ac86u>:/jffs/nexutil
    ssh admin@<address of your rt-ac86u> "/bin/chmod +x /jffs/nexutil"
    ```
### 2. Tools Installation
1. Build Makecsiparams.
    
    You can compile Makecsiparams to run on your VM or on the router. The following commands show you how to build for your VM, but you can follow the steps in the Nexutil and Tcpdump sections and cross compile Makecsiparams similarly.  You can also simply use the Makecsiparams I compiled to run on the router [here](#easy-way).
    ```
    cd ../bcm4366c0/10_10_122_20/nexmon_csi/utils/makecsiparams/
    make
    sudo ln -s $PWD/makecsiparams /usr/local/bin/mcp
    ```
2. Cross compile Tcpdump.
    ```
    # Get Merlin firmware
    git clone https://github.com/RMerl/asuswrt-merlin.ng.git

    # Cross-compile LibPCAP. It's a dependency for tcpdump.
    cd asuswrt-merlin.ng/release/src/router/libpcap
    CC=aarch64-linux-gnu-gcc AR=aarch64-linux-gnu-ar RANLIB=aarch64-linux-gnu-ranlib AS=aarch64-linux-gnu-as LD=aarch64-linux-ld NM=aarch64-linux-nm ./configure --host=x86_64-unknown-linux-gnu --with-pcap=linux
    make

    # Cross-compile Tcpdump
    cd ../tcpdump-4.4.0
    CC=aarch64-linux-gnu-gcc AR=aarch64-linux-gnu-ar RANLIB=aarch64-linux-gnu-ranlib AS=aarch64-linux-gnu-as LD=aarch64-linux-gnu-ld NM=aarch64-linux-gnu-nm ac_cv_linux_vers=2 CFLAGS=-static CPPFLAGS=-static LDFLAGS=-static ./configure --host=x86_64-unknown-linux-gnu
    make

    # Copy Tcpdump to router and make it executable
    scp tcpdump admin@<address of your rt-ac86u>:/jffs/tcpdump
    ssh admin@<Ip address of your rt-ac86u> "/bin/chmod +x /jffs/tcpdump"
    ```

### Persistence
`dhd.ko` has to be manually reloaded, and the interfaces have to be brought up everytime you reboot the router. There is a way to automate this.
1. Enable user scripts in the router's web interface: Advanced > Administration > System > Persistent JFFS2 partition > Enable user scripts
2. Create a startup script file: `touch /jffs/scripts/services-start`
3. Add the following content to the file we just created.
    ```
    #!/bin/sh

    /sbin/rmmod dhd
    /sbin/insmod /jffs/dhd.ko

    wl -i eth6 up
    wl -i eth6 radio on
    wl -i eth6 country US
    ifconfig eth6 up

    echo "services-start: Nexmon Setup Complete" > /dev/kmsg
    ```
4. Make the script executable: `chmod a+rx /jffs/scripts/services-start`

Reboot and check if our script ran with dmesg | grep "services-start". You won't need to reload router's firmware or bring up the interfaces from now on.

When using 2.4 GHz channels, use eth5 instead of eth6 in all of the steps above.
RT-AC86U uses eth6 interface for 5 GHz, and eth5 for 2.4 GHz.

## Setting up the Sender
### Asus Merlin-WRT installation and setup
- Download the latest release from https://sourceforge.net/projects/asuswrt-merlin/files/RT-AC86U/Release/
- I used [this](https://sourceforge.net/projects/asuswrt-merlin/files/RT-AC86U/Release/RT-AC86U_386.10_0.zip/download)
- Unzip
- Go to AC86U's web interface
- In Administration > Firmware Upgrade > Manual firmware upload
- Upload the RT-AC86U_386.10_0_cferom_ubi.w file
- Enable SSH
### Easy Way
This is an easy method to have all set up using precompiled files. If you have any problem is recommended to proceed the manual way.
- Download and extract precompiled files from the Tx folder.
- Copy the contents to the `/jffs/` partition of your router: `scp ./Tx/* admin@<routers-ip>:/jffs/`
- Make `nexutil`, `tx_task.sh` executable: `ssh admin@<routers-ip> "/bin/chmod +x /jffs/nexutil /jffs/mcp /jffs/tcpdump "`

### Manual way
### 1. Install Nexmon Firmware

1. Install some dependencies: `sudo apt-get install git gawk qpdf flex bison`
2. **Only necessary for x86_64 systems**, install i386 libs: 
    ```
    sudo dpkg --add-architecture i386
    sudo apt update
    sudo apt install libc6:i386 libncurses5:i386 libstdc++6:i386
    ```
3. Clone the `nexmon` base repository:  
    ```
    git clone https://github.com/seemoo-lab/nexmon.git
    ```
    _Commit `4131b84` or newer is required for compatibility._  
4. Navigate to the cloned `nexmon` directory and set a couple of environment variables.  
    ```
    cd nexmon
    source setup_env.sh
    ```
5. Run `make` to extract ucode, templateram and flashpatches from the original firmwares.
6. Navigate to the patches directory of the `bcm4366c0` chip firmware version `10.10.122.20` and clone this repository:  
    ```
    cd patches/bcm4366c0/10_10_122_20/
    git clone https://github.com/seemoo-lab/nexmon_tx_task.git
    ```
7. Enter the created subdirectory and compile the firmware patch.
    ```
    cd nexmon_tx_task
    make
    ```
    If you can perform the above steps without errors you should be able to compile the patch successfully.  
  
8. Load build environment variables, navigate to the patch directory, compile and install the patched firmware:  
    ```
    cd nexmon
    source setup_env.sh
    cd patches/bcm4366c0/10_10_122_20/nexmon_tx_task
    make install-firmware REMOTEADDR=<address of your rt-ac86u>
    ```
    This first copies a modified version of the `dhd` kernel object to the router at `/jffs/` and then unloads the current `dhd` module and instead loads the modified one.
### 2. Build and install `nexutil`
The provided `tx_task.sh` util script can be used to operate the patched firmware. It depends on `nexutil` that comes with the `nexmon` repository. Perform the following steps to build and install `nexutil` on the Asus RT-AC86U:
1. Clone the `aarch64` toolchain from Asuswrt-Merlin toolchain repository:  
    ```
    git clone https://github.com/RMerl/am-toolchains.git
    ```
2. Set the compile environment:
    ```
    export AMCC=$(pwd)/am-toolchains/brcm-arm-hnd/crosstools-aarch64-gcc-5.3-linux-4.1-glibc-2.22-binutils-2.25/usr/bin/aarch64-buildroot-linux-gnu-
    export LD_LIBRARY_PATH=$(pwd)/am-toolchains/brcm-arm-hnd/crosstools-aarch64-gcc-5.3-linux-4.1-glibc-2.22-binutils-2.25/usr/lib
    ```
3. Go to the `nexmon` repository and build `nexutil` with the previously set environment:
    ```
    cd nexmon

    cd utilities/libnexio
    ${AMCC}gcc -c libnexio.c -o libnexio.o -DBUILD_ON_RPI
    ${AMCC}ar rcs libnexio.a libnexio.o

    cd ../nexutil
    echo "typedef uint32_t uint;" > types.h
    sed -i 's/argp-extern/argp/' nexutil.c
    ${AMCC}gcc -static -o nexutil nexutil.c bcmwifi_channels.c b64-encode.c b64-decode.c -DBUILD_ON_RPI -DVERSION=0 -I. -I../libnexio -I../../patches/include -L../libnexio/ -lnexio
    ```
4. Copy the `nexutil` binary to the Asus RT-AC86U router and make it executable:  
   _Again we assume SSH is enabled on port 22 on the router, the default user name `admin`, and that you are connected to the router._  
    ```
    scp nexutil admin@<address of your rt-ac86u>:/jffs/nexutil
    ssh admin@<address of your rt-ac86u> "/bin/chmod +x /jffs/nexutil"
### 3. Install tx_task.sh Script
 The following command copies Tx/tx_task.sh to the router at /jffs/ and sets execution rights to it.
```
cd nexmon/patches/bcm4366c0/10_10_122_20/nexmon_tx_task
make install-util REMOTEADDR=<address of your rt-ac86u>
```
###  Persistence
`dhd.ko` has to be manually reloaded, and the interfaces have to be brought up everytime you reboot the router. There is a way to automate this.
1. Enable user scripts in the router's web interface: Advanced > Administration > System > Persistent JFFS2 partition > Enable user scripts
2. Create a startup script file: `touch /jffs/scripts/services-start`
3. Add the following content to the file we just created.
    ```
    #!/bin/sh

    /sbin/rmmod dhd
    /sbin/insmod /jffs/dhd.ko

    wl -i eth6 up
    wl -i eth6 radio on
    wl -i eth6 country US
    wl -i eth6 chanspec 157/80
    ifconfig eth6 up
    /jffs/tx_task.sh eth6 init
    /jffs/tx_task.sh eth6 start

    echo "services-start: Nexmon Setup Complete" > /dev/kmsg
    ```
4. Make the script executable: `chmod a+rx /jffs/scripts/services-start`

Reboot and check if our script ran with dmesg | grep "services-start". You won't need to reload router's firmware or bring up the interfaces from now on.

When using 2.4 GHz channels, use eth5 instead of eth6 in all of the steps above.
RT-AC86U uses eth6 interface for 5 GHz, and eth5 for 2.4 GHz.

Attention! The above script starts the injection right after the router is turned on.

## Setting up Docker
1. After having installed and opened Docker run the provided docker-compose file:
```
docker-compose up
```
2. Navigate to [http://localhost:15672](http://127.0.0.1:15672/)
3. Insert credentials 
    
    username: `guest` password: `guest` 
4. Create a new queue
    
    queues > Add a new queue > Name: csi_data, Durability: Transient > Add queue

# Usage
## 1. Sender
If you have followed the above instructions you should simply turn on the sender and it should start. You can change channel and bandwidth in the `services-start` and `tx_task.sh` scripts.
## 2. Receiver
<!-- ### CSI Client
Follow the [ASUS_CSI_Client](https://github.com/Davide-Zamboni/ASUS_CSI_Client) guide to install and use the tool for the receiver. -->

### PCAP dump
If you want to just have a dump of the packets in a pcap file on an external drive and extract the csi data.
1. Plug in an external usb pen drive (Raccomanded USB.3.0)
2. `./makecsiparams -c 157/80 -C 0xf -N 1 -m 00:11:22:33:44:66 —b 0x08`
3. `./nexutil -Ieth6 -s500 -b -l34 -v<output of makecsiparams>`
4. `/usr/sbin/wl -i eth6 monitor 1`
5. `./tcpdump -i eth6 dst port 5500 -w /tmp/mnt/<your usb name>/test.pcap`


# Additional tools
In the tools folder are provided different tools (They may need some adjustments to work properly):
<!-- ## RabbitMQ Local Client
A local client that connects to the RabbitMQ Server and reads packets from a provided .pcap file
### Libraries
- libpcap
- rabbitmq-c
### Usage
Simply run `make` and then `./read_pcap <ip of your rabbitmqserver`>.
Make sure to have the server running, -->

## CSV Generator
It generates a CSV file from a provided .pcap file with csi data.
### Libraries
- libpcap
### Usage
Simply run `make` and then `./read_pcap`.
Make sure to have specified the right .pcap file.


# References
- [Asus RT-AC86U: Installing Nexmon_csi #2](https://github.com/nexmonster/nexmon_csi/discussions/2)
- [Asus RT-AC86U: Building Nexmon_CSI #7](https://github.com/nexmonster/nexmon_csi/discussions/7)
- [Nexmon Channel State Information Extractor](https://github.com/seemoo-lab/nexmon_csi)
- [Nexmon Scheduled Transmission](https://github.com/seemoo-lab/nexmon_tx_task)

