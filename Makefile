.PHONY: configure kernel run

all: run

configure:
	meson setup --cross-file=kernel/build/cross.ini build/kernel kernel

	git clone https://github.com/limine-bootloader/limine build/limine
	cd build/limine && git checkout d9b062917a8b44eb4508828f8a15e144c05e6a93
	make -C build/limine limine-install

	dd if=/dev/zero of=luna.hdd bs=4M count=128
	parted -s luna.hdd mklabel msdos
	parted -s luna.hdd mkpart primary 2048s 100%

	./build/limine/limine-install ./build/limine/limine.bin luna.hdd
	

kernel:
	ninja -C build/kernel
	
	echfs-utils -m -p0 luna.hdd format 512
	echfs-utils -m -p0 luna.hdd import limine.cfg limine.cfg
	echfs-utils -m -p0 luna.hdd import build/kernel/luna.bin boot/luna.bin

run: kernel
	# -cpu qemu64,level=11,+la57 To enable 5 Level Paging, does not work with KVM
	qemu-system-x86_64 -enable-kvm -machine q35 -smp 4 -hda luna.hdd -serial file:/dev/stdout -monitor stdio -no-reboot -no-shutdown

