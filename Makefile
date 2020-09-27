.PHONY: configure kernel run

all: run

configure:
	meson setup --cross-file=kernel/build/cross.ini build/kernel kernel

	git clone https://github.com/limine-bootloader/limine build/limine
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
	qemu-system-x86_64 -enable-kvm -smp 4 -hda luna.hdd -debugcon file:/dev/stdout -monitor stdio -no-reboot -no-shutdown

