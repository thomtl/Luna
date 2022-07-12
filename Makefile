.PHONY: all configure kernel assets bios test_guest run debug

all: run

configure:
	meson setup --cross-file=kernel/build/cross.ini build/kernel kernel

	git clone https://github.com/limine-bootloader/limine build/limine --branch=v3.4.3-binary --depth=1
	make -C build/limine limine-deploy

	git clone https://git.seabios.org/seabios.git build/seabios
	cd build/seabios && git checkout ef88eeaf052c8a7d28c5f85e790c5e45bcffa45e
	cp misc/seabios-config.ini build/seabios/.config
	make -C build/seabios -j8

	git clone https://github.com/torvalds/linux --branch v5.17 --depth 1 build/linux
	cp misc/linux-config.ini build/linux/.config
	make -C build/linux -j8

	dd if=/dev/zero of=luna.hdd bs=4M count=16
	parted -s luna.hdd mklabel msdos
	parted -s luna.hdd mkpart primary 2048s 100%

	./build/limine/limine-deploy luna.hdd

	
kernel:
	ninja -C build/kernel
	
	echfs-utils -m -p0 luna.hdd format 512
	echfs-utils -m -p0 luna.hdd import misc/limine.cfg limine.cfg
	echfs-utils -m -p0 luna.hdd import build/limine/limine.sys limine.sys

	echfs-utils -m -p0 luna.hdd import build/kernel/luna.bin boot/luna.bin

	

assets:
	echfs-utils -m -p0 luna.hdd import misc/cursor.bmp luna/assets/cursor.bmp

# convert downarrow.png -alpha set -define bmp:format=bmp4 downarrow.bmp
	echfs-utils -m -p0 luna.hdd import misc/bottomarrow.bmp luna/assets/bottomarrow.bmp
	echfs-utils -m -p0 luna.hdd import misc/toparrow.bmp luna/assets/toparrow.bmp
	echfs-utils -m -p0 luna.hdd import misc/uparrow.bmp luna/assets/uparrow.bmp
	echfs-utils -m -p0 luna.hdd import misc/downarrow.bmp luna/assets/downarrow.bmp

	echfs-utils -m -p0 luna.hdd import misc/bottomarrow_clicked.bmp luna/assets/bottomarrow_clicked.bmp
	echfs-utils -m -p0 luna.hdd import misc/toparrow_clicked.bmp luna/assets/toparrow_clicked.bmp
	echfs-utils -m -p0 luna.hdd import misc/uparrow_clicked.bmp luna/assets/uparrow_clicked.bmp
	echfs-utils -m -p0 luna.hdd import misc/downarrow_clicked.bmp luna/assets/downarrow_clicked.bmp

bios:
	make -C build/seabios -j8
	
	echfs-utils -m -p0 luna.hdd import build/seabios/out/bios.bin luna/bios.bin
	echfs-utils -m -p0 luna.hdd import build/seabios/out/vgabios.bin luna/vgabios.bin

test_guest:
	./misc/create_linux_guest.sh
	echfs-utils -m -p0 luna.hdd import build/linux-guest.iso disk.bin

# -cpu qemu64,level=11,+la57 To enable 5 Level Paging, does not work with KVM
# Intel IOMMU: -device intel-iommu,aw-bits=48
# AMD IOMMU: -device amd-iommu
QEMU_FLAGS := -enable-kvm -cpu host,+invtsc -device intel-iommu,aw-bits=48 -machine q35 -global hpet.msi=true -smp 4 -m 512M -hda luna.hdd -serial file:/dev/stdout -monitor stdio -no-reboot -no-shutdown \
			  -device ich9-intel-hda -device hda-output \
			  -device qemu-xhci -device usb-mouse -device usb-kbd

run: kernel assets bios test_guest
	qemu-system-x86_64 ${QEMU_FLAGS}

debug: kernel assets bios test_guest
	qemu-system-x86_64 ${QEMU_FLAGS} -s -S &

	gdb build/kernel/luna.bin -ex "target remote localhost:1234" -ex "set disassemble-next-line on" -ex "set disassembly-flavor intel"

