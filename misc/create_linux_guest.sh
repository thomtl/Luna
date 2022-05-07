#!/bin/bash

mkdir -p tmp/boot/grub
cp build/linux/arch/x86/boot/bzImage tmp/vmlinuz
echo -e "set default=0\nset timeout=1\nmenuentry \"Linux\" {\n linux /vmlinuz earlyprintk=serial,ttyS0,9600 console=ttyS0,9600n8\n}" >> tmp/boot/grub/grub.cfg

grub-mkrescue -v -o build/linux-guest.iso tmp

rm -rf tmp