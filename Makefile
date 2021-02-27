.PHONY: configure kernel bios test_guest run

all: run

configure:
	meson setup --cross-file=kernel/build/cross.ini build/kernel kernel

	git clone https://github.com/limine-bootloader/limine build/limine
	cd build/limine && git checkout 0c6baf3591b59dbe35f668103acdbbe15779e6b8
	make -C build/limine limine-install

	git clone https://git.seabios.org/seabios.git build/seabios
	cd build/seabios && git checkout ef88eeaf052c8a7d28c5f85e790c5e45bcffa45e
	cp misc/seabios-config.ini build/seabios/.config
	make -C build/seabios

	dd if=/dev/zero of=luna.hdd bs=4M count=128
	parted -s luna.hdd mklabel msdos
	parted -s luna.hdd mkpart primary 2048s 100%

	./build/limine/limine-install ./build/limine/limine.bin luna.hdd

	echfs-utils -m -p0 luna.hdd format 512
	echfs-utils -m -p0 luna.hdd import misc/limine.cfg limine.cfg

kernel:
	ninja -C build/kernel
	
	echfs-utils -m -f -p0 luna.hdd import build/kernel/luna.bin boot/luna.bin

bios:
	echfs-utils -m -f -p0 luna.hdd import build/seabios/out/bios.bin luna/bios.bin

test_guest:
	nasm misc/test_guest.asm -f bin -o build/test_guest.hdd

	echfs-utils -m -f -p0 luna.hdd import build/test_guest.hdd disk.bin

# -cpu qemu64,level=11,+la57 To enable 5 Level Paging, does not work with KVM
# Intel IOMMU: -device intel-iommu,aw-bits=48
# AMD IOMMU: -device amd-iommu
QEMU_FLAGS := -enable-kvm -cpu host -device intel-iommu,aw-bits=48 -machine q35 -global hpet.msi=true -smp 4 -hda luna.hdd -serial file:/dev/stdout -monitor stdio -no-reboot -no-shutdown \
				-device ich9-intel-hda -device hda-output

run: kernel bios test_guest
	
	qemu-system-x86_64 ${QEMU_FLAGS}

debug: kernel bios test_guest
	qemu-system-x86_64 ${QEMU_FLAGS} -s -S &

	gdb build/kernel/luna.bin -ex "target remote localhost:1234" -ex "set disassemble-next-line on" -ex "set disassembly-flavor intel"

